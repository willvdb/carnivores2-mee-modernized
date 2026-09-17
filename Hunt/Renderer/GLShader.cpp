// ==========================================================================
// GLShader.cpp — OpenGL shader management for Carnivores 2 ME
//
// Full implementation of GLShader with external file loading, uniform
// caching, and error logging.
// ==========================================================================

#include "Hunt.h"
#include "GLShader.h"
#include "../../Shared/LegacyPath.h"

#ifdef _gl

#include <cstdio>
#include <vector>

// --------------------------------------------------------------------------
// Internal helper: read an entire text file into a std::string.
// Returns an empty string on failure (and logs the error).
// --------------------------------------------------------------------------
static std::string ReadTextFile(const char* path)
{
    const auto resolved = LegacyPath::Resolve(path);
    if (!resolved) {
        LOG_ERROR("GLShader: %s", resolved.Message().c_str());
        return {};
    }
    FILE* fp = std::fopen(resolved.path.string().c_str(), "rb");
    if (!fp) {
        LOG_ERROR("GLShader: failed to open '%s'", path);
        return {};
    }

    // Seek to end to determine size
    std::fseek(fp, 0, SEEK_END);
    long size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);

    if (size <= 0) {
        LOG_ERROR("GLShader: '%s' is empty", path);
        std::fclose(fp);
        return {};
    }

    std::string contents;
    contents.resize(static_cast<size_t>(size));
    size_t bytesRead = std::fread(&contents[0], 1, static_cast<size_t>(size), fp);
    std::fclose(fp);

    if (bytesRead != static_cast<size_t>(size)) {
        LOG_ERROR("GLShader: short read on '%s' (%zu of %ld)", path, bytesRead, size);
        return {};
    }

    // Strip trailing nulls and carriage returns (Windows line endings)
    while (!contents.empty() && (contents.back() == '\0' || contents.back() == '\r'))
        contents.pop_back();

    return contents;
}

// ==========================================================================
// GLShader implementation
// ==========================================================================

GLShader::GLShader()
    : m_Program(0)
{
}

GLShader::~GLShader()
{
    Release();
}

GLShader::GLShader(GLShader&& other) noexcept
    : m_Program(other.m_Program)
    , m_uniformLocations(std::move(other.m_uniformLocations))
{
    other.m_Program = 0;
}

GLShader& GLShader::operator=(GLShader&& other) noexcept
{
    if (this != &other) {
        Release();
        m_Program = other.m_Program;
        m_uniformLocations = std::move(other.m_uniformLocations);
        other.m_Program = 0;
    }
    return *this;
}

// --------------------------------------------------------------------------
// LoadFromFile
// --------------------------------------------------------------------------
bool GLShader::LoadFromFile(const char* vertPath, const char* fragPath)
{
    Release();

    std::string vertSrc = ReadTextFile(vertPath);
    if (vertSrc.empty()) {
        LOG_ERROR("GLShader: failed to read vertex shader '%s'", vertPath);
        return false;
    }

    std::string fragSrc = ReadTextFile(fragPath);
    if (fragSrc.empty()) {
        LOG_ERROR("GLShader: failed to read fragment shader '%s'", fragPath);
        return false;
    }

    return LoadFromSource(vertSrc.c_str(), fragSrc.c_str());
}

// --------------------------------------------------------------------------
// LoadFromSource
// --------------------------------------------------------------------------
bool GLShader::LoadFromSource(const char* vertSrc, const char* fragSrc)
{
    Release();

    GLuint vs = CompileShader(GL_VERTEX_SHADER, vertSrc);
    if (!vs) {
        return false;
    }

    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragSrc);
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }

    m_Program = LinkProgram(vs, fs);
    // LinkProgram deletes vs and fs on success; on failure we need to clean up
    if (!m_Program) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return false;
    }

    m_uniformLocations.clear();
    return true;
}

// --------------------------------------------------------------------------
// CompileShader (private)
// --------------------------------------------------------------------------
GLuint GLShader::CompileShader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    if (!shader) {
        LOG_ERROR("GLShader: glCreateShader failed");
        return 0;
    }

    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    if (!CheckCompileErrors(shader, (type == GL_VERTEX_SHADER) ? "VERTEX" : "FRAGMENT")) {
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

// --------------------------------------------------------------------------
// LinkProgram (private)
// --------------------------------------------------------------------------
GLuint GLShader::LinkProgram(GLuint vertexShader, GLuint fragmentShader)
{
    GLuint program = glCreateProgram();
    if (!program) {
        LOG_ERROR("GLShader: glCreateProgram failed");
        return 0;
    }

    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    if (!CheckLinkErrors(program)) {
        glDeleteProgram(program);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return 0;
    }

    // Shader objects are no longer needed after linking
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

// --------------------------------------------------------------------------
// CheckCompileErrors (private)
// --------------------------------------------------------------------------
bool GLShader::CheckCompileErrors(GLuint shader, const char* typeStr)
{
    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (!success) {
        char infoLog[4096];
        GLsizei logLength = 0;
        glGetShaderInfoLog(shader, sizeof(infoLog), &logLength, infoLog);
        infoLog[sizeof(infoLog) - 1] = '\0';
        LOG_ERROR("GLShader: %s shader compilation failed: %s", typeStr, infoLog);
    }

    return success != GL_FALSE;
}

// --------------------------------------------------------------------------
// CheckLinkErrors (private)
// --------------------------------------------------------------------------
bool GLShader::CheckLinkErrors(GLuint program)
{
    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);

    if (!success) {
        char infoLog[4096];
        GLsizei logLength = 0;
        glGetProgramInfoLog(program, sizeof(infoLog), &logLength, infoLog);
        infoLog[sizeof(infoLog) - 1] = '\0';
        LOG_ERROR("GLShader: program linking failed: %s", infoLog);
    }

    return success != GL_FALSE;
}

// --------------------------------------------------------------------------
// Use / Release
// --------------------------------------------------------------------------
void GLShader::Use() const
{
    if (m_Program) {
        glUseProgram(m_Program);
    }
}

void GLShader::Release()
{
    if (m_Program) {
        glDeleteProgram(m_Program);
        m_Program = 0;
    }
    m_uniformLocations.clear();
}

// --------------------------------------------------------------------------
// GetUniformLocation (private, with caching)
// --------------------------------------------------------------------------
GLint GLShader::GetUniformLocation(const std::string& name)
{
    if (!m_Program) return -1;

    const auto it = m_uniformLocations.find(name);
    if (it != m_uniformLocations.end()) {
        return it->second;
    }

    GLint location = glGetUniformLocation(m_Program, name.c_str());
    m_uniformLocations.emplace(name, location);
    return location;
}

// --------------------------------------------------------------------------
// Uniform setters
// --------------------------------------------------------------------------
void GLShader::SetUniformVec2(const std::string& name, float x, float y)
{
    GLint loc = GetUniformLocation(name);
    if (loc != -1) glUniform2f(loc, x, y);
}

void GLShader::SetUniformVec3(const std::string& name, float x, float y, float z)
{
    GLint loc = GetUniformLocation(name);
    if (loc != -1) glUniform3f(loc, x, y, z);
}

void GLShader::SetUniformFloat(const std::string& name, float value)
{
    GLint loc = GetUniformLocation(name);
    if (loc != -1) glUniform1f(loc, value);
}

void GLShader::SetUniformInt(const std::string& name, int value)
{
    GLint loc = GetUniformLocation(name);
    if (loc != -1) glUniform1i(loc, value);
}

void GLShader::SetUniformMat4(const std::string& name, const float* matrix)
{
    GLint loc = GetUniformLocation(name);
    if (loc != -1) glUniformMatrix4fv(loc, 1, GL_FALSE, matrix);
}

#endif // _gl
