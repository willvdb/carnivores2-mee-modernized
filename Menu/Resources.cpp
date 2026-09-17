/***************************************************
* AtmosFear 2.1
* Characters.cpp
*
* Resources, Files and Memory Management
*
*/

#include "Hunt.h"
#include "ProfileSerialization.h"
#include "Targa.h"

#include <iostream>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <set>
#include <vector>
#include "Core/ConfigText.h"


class script_error : public std::exception
{
private:
	std::string m_What;
	uint32_t	m_Line;
	std::string m_Where;

public:

	script_error(const std::string& _what, const std::string& _where = "", uint32_t _line = 0) :
		m_What(_what),
		m_Line(_line),
		m_Where(_where)
	{}

	std::string what()
	{
		std::stringstream ss;
		ss << "Where: " << m_Where << "\r\n";
		ss << "At Line: " << m_Line << "\r\n";
		ss << m_What << "\r\n";
		return ss.str();
	}
};


uint32_t g_ScriptLine = 0;


void ReadWeapons(FILE*);
void ReadCharacters(FILE*);
void ReadAccessories(FILE*);
void LoadC2Maps();


/*
! DEPRECATED !
This function is deprecated and set for removal in future versions
! WARNING !
This is a kludge meant to support the original games method of loading resources via the _RES file.

Construct an AreaInfo object and return it with the correct information stored for use by the menu,
this is a backwards compatability function to support the original vanilla _RES and file structure.
*/
AreaInfo MakeOldAreaInfo(int index, int price)
{
	std::stringstream ss;
	AreaInfo a;

	std::cout << "MakeOldAreaInfo(index=" << index << ", price=" << price << ")" << std::endl;

	// Set the cost (required score) rank and the dinosaurs available
	a.m_Price = price;
	a.m_Rank = RANK_BEGINNER; // Not used in vanilla
	a.m_DinosAvail.clear(); // Not used currently but empty would mean [all]

	// Load the description
	ss << "huntdat/menu/txt/area" << index << ".txt";
	std::cout << "  Desc file: " << ss.str();
	std::ifstream f(ss.str());

	if (f.is_open())
	{
		while (!f.eof())
		{
			std::string line = "";
			std::getline(f, line);
			a.m_Description.push_back(line);
		}
		f.close();
		std::cout << " -> OK (" << a.m_Description.size() << " lines)" << std::endl;
	}
	else
	{
		std::cout << " -> FAILED" << std::endl;
	}

	ss.str(""); ss.clear();
	ss << "Area " << index;

	// Set the readable name
	if (a.m_Description.size() > 0)
		a.m_Name = a.m_Description[0];
	else
		a.m_Name = ss.str();

	std::cout << "  Name: " << a.m_Name << std::endl;

	// Set the project name (filename for .MAP and .RSC files)
	ss.str(""); ss.clear();
	ss << "area" << index;
	a.m_ProjectName = ss.str();

	// Load the map thumbnail
	ss.str(""); ss.clear();
	ss << "huntdat/menu/pics/area" << index << ".tga";
	std::cout << "  Thumb: " << ss.str();
	LoadPicture(a.m_Thumbnail, ss.str());
	std::cout << " -> " << (a.m_Thumbnail.IsValid() ? "OK" : "FAILED") << std::endl;

	// Make sure the map exists. m_ProjectName stays "area6" (the slot's logical
	// identity, used by launch assembly and saved-hunt restore), but the vanilla
	// sixth slot stores its assets as external.map/.rsc — so whichever basename
	// actually resolved is recorded in m_MapFile and used at launch. Launching a
	// name whose files do not exist halts the engine with "Error opening resource
	// file" (reproduced with prj=huntdat/areas/area6 on stock data), and launching
	// "external" without parser-side aliasing breaks the engine's script area
	// filtering (0xC0000005); ScriptParser.cpp now aliases external->area6, so the
	// resolved basename is always launchable.
	std::string mapName;
	if (index == 6) {
		mapName = "huntdat/areas/external.map";
		std::cout << "  Map:   " << mapName;
		f.open(mapName.c_str());
		a.m_Valid = f.is_open();
		std::cout << " -> " << (a.m_Valid ? "OK" : "FAILED") << std::endl;
		f.close();
		if (a.m_Valid) {
			a.m_MapFile = "external";
		} else {
			mapName = "huntdat/areas/area6.map";
			std::cout << "  Map:   " << mapName << " (fallback)";
			f.open(mapName.c_str());
			a.m_Valid = f.is_open();
			std::cout << " -> " << (a.m_Valid ? "OK" : "FAILED") << std::endl;
			f.close();
			if (a.m_Valid) a.m_MapFile = "area6";
		}
	} else {
		ss.str(""); ss.clear();
		ss << "huntdat/areas/area" << index << ".map";
		mapName = ss.str();
		std::cout << "  Map:   " << mapName;
		f.open(mapName.c_str());
		a.m_Valid = f.is_open();
		std::cout << " -> " << (a.m_Valid ? "OK" : "FAILED") << std::endl;
		f.close();
	}

	std::cout << "  Result: " << (a.m_Valid ? "VALID" : "INVALID (will be skipped)") << std::endl;
	if (a.m_Valid && !a.m_MapFile.empty())
		std::cout << "  Launch: huntdat/areas/" << a.m_MapFile << std::endl;

	return a;
}


/*
 * Skip a nested {} block in the _RES script.
 * Call this when encountering 'overwrite' or 'addition' keywords.
 * Handles nested braces correctly.
 */
static void SkipNestedBlock(FILE* stream, char* line, int maxLine)
{
    int depth = 0;

    // Count opening braces on the current line
    for (char* p = line; *p; p++) {
        if (*p == '{') depth++;
        if (*p == '}') depth--;
    }

    // Read lines until we find the matching closing brace
    while (depth > 0 && fgets(line, maxLine, stream)) {
        g_ScriptLine++;
        for (char* p = line; *p; p++) {
            if (*p == '{') depth++;
            if (*p == '}') depth--;
        }
    }
}


void ReadWeapons(FILE* stream)
{
	char line[256], * value;
	std::string sline = "";

	while (fgets(line, 255, stream))
	{
		g_ScriptLine++;
		if (strstr(line, "}")) break;
		if (strstr(line, "{"))
		{
			WeapInfo wi;

			std::stringstream spp;

			while (fgets(line, 255, stream))
			{
				g_ScriptLine++;
				sline = line;

				if (strstr(line, "}")) break;
				if (sline.empty()) continue;
				if (line[0] == ';') continue;

				// Skip overwrite/addition blocks (C2ME nested blocks)
				if (strstr(line, "overwrite") || strstr(line, "addition")) {
					if (strstr(line, "{")) {
						SkipNestedBlock(stream, line, 255);
					}
					continue;
				}

				value = strstr(line, "=");
				if (!value)
					throw script_error("Was expecting member assignment.", "ReadWeapons()", g_ScriptLine);
				value++;

				if (strstr(line, "power"))  wi.m_Power = static_cast<float>(atof(value));
				if (strstr(line, "prec"))   wi.m_Prec = static_cast<float>(atof(value));
				if (strstr(line, "loud"))   wi.m_Loud = static_cast<float>(atof(value));
				if (strstr(line, "rate"))   wi.m_Rate = static_cast<float>(atof(value));
				if (strstr(line, "shots"))  wi.m_Shots = atoi(value);
				if (strstr(line, "reload")) wi.m_Reload = atoi(value);
				if (strstr(line, "trace"))  wi.m_TraceC = atoi(value) - 1;
				if (strstr(line, "optic"))  wi.m_Optic = static_cast<float>(atof(value));
				if (strstr(line, "fall"))   wi.m_Fall = atoi(value);
				if (strstr(line, "price"))	wi.m_Price = atoi(value);
				if (strstr(line, "rank"))	wi.m_Rank = atoi(value);

				if (strstr(line, "name")) {
					value = strstr(line, "'"); if (!value) throw std::runtime_error("Script loading error");
					value[strlen(value) - 2] = 0;
					wi.m_Name = &value[1];
				}

				if (strstr(line, "file")) {
					value = strstr(line, "'"); if (!value) throw std::runtime_error("Script loading error");
					value[strlen(value) - 2] = 0;
					wi.m_FilePath = &value[1];
				}

				if (strstr(line, "pic")) {
					value = strstr(line, "'"); if (!value) throw std::runtime_error("Script loading error");
					value[strlen(value) - 2] = 0;
					wi.m_BulletFilePath = &value[1];
				}
			}

			spp << "huntdat/menu/pics/weapon" << (g_WeapInfo.size() + 1) << ".tga";
			LoadPicture(wi.m_Thumbnail, spp.str());

			g_WeapInfo.push_back(wi);
		}

	}

}


void ReadCharacters(FILE* stream)
{
	char line[256], * value;
	while (fgets(line, 255, stream))
	{
		g_ScriptLine++;
		if (strstr(line, "}")) break;
		if (strstr(line, "{"))
		{
			DinoInfo di;

			while (fgets(line, 255, stream))
			{
				g_ScriptLine++;

				if (strstr(line, "}"))
				{
					//AI_to_CIndex[DinoInfo[TotalC].AI] = TotalC;
					//TotalC++;
					break;
				}

				// Skip overwrite/addition blocks (C2ME nested blocks)
				if (strstr(line, "overwrite") || strstr(line, "addition")) {
					if (strstr(line, "{")) {
						SkipNestedBlock(stream, line, 255);
					}
					continue;
				}

				// Skip other C2ME sub-blocks that the menu doesn't need
				if (strstr(line, "spawninfo") || strstr(line, "spawngroup") ||
				    strstr(line, "killtype") || strstr(line, "tropinfo") ||
				    strstr(line, "deathtype") || strstr(line, "idlegroup") ||
				    strstr(line, "packinfo") || strstr(line, "packgroup") ||
				    strstr(line, "waterIgroup")) {
					if (strstr(line, "{")) {
						SkipNestedBlock(stream, line, 255);
					}
					continue;
				}

				value = strstr(line, "=");
				if (!value)
					throw script_error("Was expecting member assignment.", "ReadCharacters()", g_ScriptLine);
				value++;

				if (strstr(line, "mass")) di.m_Mass = static_cast<float>(atof(value));
				if (strstr(line, "length")) di.m_Length = static_cast<float>(atof(value));
				if (strstr(line, "radius")) di.m_Radius = static_cast<float>(atof(value));
				if (strstr(line, "health")) di.m_BaseHealth = atoi(value);
				if (strstr(line, "basescore")) di.m_BaseScore = atoi(value);
				if (strstr(line, "ai")) di.m_AI = atoi(value);
				if (strstr(line, "smell")) di.m_SmellK = static_cast<float>(atof(value));
				if (strstr(line, "hear")) di.m_HearK = static_cast<float>(atof(value));
				if (strstr(line, "look")) di.m_LookK = static_cast<float>(atof(value));
				// -> Safety Check
				if (strstr(line, "smellk")) di.m_SmellK = static_cast<float>(atof(value));
				if (strstr(line, "heark")) di.m_HearK = static_cast<float>(atof(value));
				if (strstr(line, "lookk")) di.m_LookK = static_cast<float>(atof(value));
				// <- End
				if (strstr(line, "shipdelta")) di.m_ShDelta = static_cast<float>(atof(value));
				if (strstr(line, "scale0")) di.m_BaseScale = atoi(value);
				if (strstr(line, "scaleA")) di.m_ScaleA = atoi(value);
				if (strstr(line, "danger")) di.m_DangerCall = true;

				if (strstr(line, "name"))
				{
					value = strstr(line, "'");
					if (!value) throw std::runtime_error("Script loading error");
					value[strlen(value) - 2] = 0;
					di.m_Name = &value[1];
				}

				if (strstr(line, "file"))
				{
					value = strstr(line, "'");
					if (!value) throw std::runtime_error("Script loading error");
					value[strlen(value) - 2] = 0;
					di.m_FilePath = &value[1];
				}

				if (strstr(line, "pic"))
				{
					value = strstr(line, "'");
					if (!value) throw std::runtime_error("Script loading error");
					value[strlen(value) - 2] = 0;
					di.m_PicturePath = &value[1];
				}
			}

			// Only add huntable dinosaurs (AI >= 10) to the menu list
			if (di.m_AI >= 10)
			{
				std::stringstream spp;
				spp << "huntdat/menu/pics/dino" << (di.m_AI - 9) << ".tga";
				LoadPicture(di.m_Thumbnail, spp.str());

				spp.str(""); spp.clear();
				spp << "huntdat/menu/pics/dino" << (di.m_AI - 9) << "no.tga";
				if (!LoadPicture(di.m_ThumbnailHidden, spp.str()))
				{
					di.m_ThumbnailHidden = di.m_Thumbnail;
				}

				g_DinoInfo.push_back(di);
			}
		}

	}
}


void ReadAreas(FILE* stream)
{
	char line[256], * value;
	while (fgets(line, 255, stream))
	{
		if (strstr(line, "}")) break;
		if (strstr(line, "{"))
		{
			AreaInfo area;

			while (fgets(line, 255, stream))
			{
				if (strstr(line, "}"))
				{
					break;
				}

				value = strstr(line, "=");
				if (!value)
					throw std::runtime_error("Script loading error");
				value++;

				if (strstr(line, "price")) area.m_Price = atoi(value);
				if (strstr(line, "rank"))  area.m_Rank = atoi(value);

				if (strstr(line, "name"))
				{
					value = strstr(line, "'");
					if (!value) throw std::runtime_error("Script loading error");
					value[strlen(value) - 2] = 0;
					area.m_Name = &value[1];
				}

				if (strstr(line, "pname"))
				{
					value = strstr(line, "'"); if (!value) throw std::runtime_error("Script loading error");
					value[strlen(value) - 2] = 0;
					area.m_ProjectName = &value[1];
				}

				if (strstr(line, "thumbnail"))
				{
					value = strstr(line, "'");
					if (!value) throw std::runtime_error("Script loading error");
					value[strlen(value) - 2] = 0;
					///TODO: Load TPicture
					//strcpy(area.Thumbnail, &value[1]);
				}
			}

			g_AreaInfo.push_back(area);
		}

	}
}


/*
 * ReadAccessories()
 *
 * Parses the 'accessories { ... }' block in _MENU.TXT / _RES.TXT.
 * Format (one entry per line, key = value):
 *
 *     accessories
 *     {
 *         camo    = 0.85
 *         radar   = 0.70
 *         scent   = 0.80
 *         double  = 1.0
 *         tranq   = 1.25
 *         observe = 1.0
 *     }
 *     .
 *
 * Keys correspond to the command-line flag (without leading '-').
 * Populates the global g_AccessoryScoreMods map. Missing keys fall
 * back to hardcoded defaults applied in LoadResources().
 */
void ReadAccessories(FILE* stream)
{
	char line[256];
	uint32_t count = 0;

	while (fgets(line, 255, stream))
	{
		g_ScriptLine++;
		if (line[0] == '.') break; // end of block

		// Find the '=' separator
		const char* eq = strchr(line, '=');
		if (!eq) continue; // not a key=value line

		// Extract key
		std::string key(line, eq - line);
		// Trim whitespace from key
		while (!key.empty() && (key.back() == ' ' || key.back() == '\t' || key.back() == '\r' || key.back() == '\n'))
			key.pop_back();
		size_t kstart = key.find_first_not_of(" \t");
		if (kstart != std::string::npos) key = key.substr(kstart);
		if (key.empty() || key[0] == '#') continue; // empty or comment

		// Extract value
		std::string value(eq + 1);
		// Strip trailing whitespace
		while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' || value.back() == '\n'))
			value.pop_back();
		// Strip leading whitespace
		size_t vstart = value.find_first_not_of(" \t");
		if (vstart != std::string::npos) value = value.substr(vstart);
		// Strip inline comments
		size_t comment = value.find('#');
		if (comment != std::string::npos) value = value.substr(0, comment);
		// Trim again after comment strip
		while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' || value.back() == '\n'))
			value.pop_back();

		if (value.empty()) continue;

		float mod = static_cast<float>(atof(value.c_str()));
		g_AccessoryScoreMods[key] = mod;
		count++;
		std::cout << "  accessory[" << key << "] = " << mod << std::endl;
	}

	std::cout << "Loaded " << count << " accessory score modifiers" << std::endl;
}


/*
 * LookupAccessoryScoreMod()
 *
 * Returns the modder-defined score multiplier for the given accessory
 * key, or the hardcoded default if the key was not present in the
 * accessories { ... } block.
 *
 * Keys: "camo", "radar", "scent", "double", "supply", "tranq", "observe"
 */
static float LookupAccessoryScoreMod(const std::string& key, float defaultValue)
{
	auto it = g_AccessoryScoreMods.find(key);
	if (it != g_AccessoryScoreMods.end()) {
		return it->second;
	}
	return defaultValue;
}


void ReadPrices(FILE* stream)
{
	uint32_t CurA = 0;
	uint32_t CurW = 0;
	uint32_t CurD = 0;
	uint32_t CurU = 0;
	g_AccessoryPrices.clear();  // reset on each script load
	char line[256], * value;

	// Initialise the `CurD` variable to the first huntable index
	for (uint32_t i = 0; i < g_DinoInfo.size(); i++)
	{
		if (g_DinoInfo.at(i).m_AI == 10)
		{
			CurD = i;
			break;
		}
	}

	while (fgets(line, 255, stream))
	{
		g_ScriptLine++;
		std::string line_str = line;
		if (line_str.empty()) { continue; }
		if (line_str.compare("\n") == 0) { continue; }
		if (line_str.compare("\r\n") == 0) { continue; }
		if (strstr(line, "}")) { break; }

		value = strstr(line, "=");
		if (!value) { continue; }
		value++;

		// TODO: Add in error checking
		//throw script_error("Was expecting member assignment.", "ReadPrices()", g_ScriptLine);

		if (strstr(line, "start")) {
                g_StartCredits = static_cast<int>(atoi(value));
		}
		else if (strstr(line, "area")) {
			CurA++;  // Area indices start at 1
			g_AreaInfo.push_back(MakeOldAreaInfo(CurA, static_cast<int>(atoi(value))));
			auto a = g_AreaInfo.end() - 1;
			if (!a->m_Valid)
				g_AreaInfo.pop_back();
		}
		else if (strstr(line, "dino")) {
			g_DinoInfo[CurD].m_Price = static_cast<int>(atoi(value));
			CurD++;
		}
		else if (strstr(line, "weapon")) {
			g_WeapInfo[CurW].m_Price = static_cast<int>(atoi(value));
			CurW++;
		}
		else if (strstr(line, "acces")) {
			g_AccessoryPrices.push_back(static_cast<int32_t>(atoi(value)));
			CurU++;
		}
	}
}


/*
 * LoadC2Maps()
 *
 * Discover and parse .c2map custom-map descriptor files in
 * HUNTDAT/AREAS/. The format is the MEE format (Adelphospro 4.22.09):
 *
 *     info
 *     {
 *         name    = 'Delphaeus Hills'
 *         mapfile = 'huntdat\\areas\\area1'
 *         rscfile = 'huntdat\\areas\\area1.rsc'
 *         pic     = 'huntdat\\menu\\pics\\area1.tga'
 *         text    = 'huntdat\\menu\\txt\\area1.txt'
 *         price   = 20
 *     }
 *     .
 *
 * Unlike the MEE parser, we use a proper key/value tokeniser (the MEE
 * code does strstr(line, "txt") which only works because "text" is a
 * substring of "txt" -- a real footgun if anyone renames a key).
 *
 * Discovered maps are appended to g_AreaInfo unless the map's project
 * name already exists (the script-defined entry wins). Maps whose
 * .MAP file is missing are skipped with a warning.
 */
void LoadC2Maps()
{
	namespace fs = std::filesystem;
	const std::string areasDir = "huntdat/areas";

	std::error_code ec;
	if (!fs::is_directory(areasDir, ec)) {
		// No areas directory -- not a hard error, modder may not have any c2maps
		return;
	}

	int discovered = 0;
	int loaded = 0;
	int skipped = 0;
	int duplicates = 0;

	for (const auto& entry : fs::directory_iterator(areasDir, ec)) {
		if (ec) break;
		if (!entry.is_regular_file()) continue;
		if (entry.path().extension() != ".c2map") continue;

		discovered++;
		const std::string c2mapPath = entry.path().string();

		std::ifstream f(c2mapPath);
		if (!f.is_open()) {
			std::cout << "LoadC2Maps: cannot open '" << c2mapPath << "'" << std::endl;
			skipped++;
			continue;
		}

		AreaInfo area;
		area.m_Valid = false;
		area.m_Rank = RANK_BEGINNER;
		bool inBlock = false;
		std::string mapfile;

		std::string line;
		while (std::getline(f, line)) {
			// Trim leading whitespace for prefix checks
			size_t start = line.find_first_not_of(" \t\r\n");
			if (start == std::string::npos) continue;
			if (line[start] == '#') continue; // comment

			if (line[start] == '.') break; // EOF marker

			if (!inBlock) {
				// The c2map format puts 'info' on one line and '{' on the next,
				// so we accept either: 'info {', 'info', or '{' alone (after
				// we've seen 'info' on the previous iteration).
				bool hasInfo = line.find("info", start) != std::string::npos;
				bool hasOpenBrace = line.find('{', start) != std::string::npos;
				if (hasInfo || hasOpenBrace) {
					inBlock = true;
				}
				continue;
			}

			// Inside the info { ... } block
			if (line.find('}', start) != std::string::npos) break;

			auto eq = line.find('=', start);
			if (eq == std::string::npos) continue;

			std::string key = line.substr(start, eq - start);
			// Trim trailing whitespace on the key
			while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
				key.pop_back();

			std::string value = line.substr(eq + 1);
			// Trim leading whitespace on the value
			size_t vstart = value.find_first_not_of(" \t");
			if (vstart != std::string::npos) value = value.substr(vstart);
			// Strip trailing whitespace + comments
			size_t comment = value.find('#');
			if (comment != std::string::npos) value = value.substr(0, comment);
			while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' || value.back() == '\n'))
				value.pop_back();

			// Strip surrounding single quotes from string values
			if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
				value = value.substr(1, value.size() - 2);
			}

			if (key == "price") {
				area.m_Price = std::atoi(value.c_str());
			}
			else if (key == "rank") {
				area.m_Rank = std::atoi(value.c_str());
			}
			else if (key == "name") {
				area.m_Name = value;
			}
			else if (key == "mapfile") {
				mapfile = value;
				// Project name is the .MAP filename without extension.
				// Normalize backslashes to forward slashes first so
				// fs::path behaves the same on Windows and POSIX.
				std::string norm = value;
				for (auto& c : norm) if (c == '\\') c = '/';
				fs::path mp(norm);
				area.m_ProjectName = mp.stem().string();
			}
			else if (key == "rscfile") {
				// Stored in m_ProjectName + ".rsc" already, but keep raw for validation
				// (we don't currently validate .rsc, .MAP is the hard requirement)
			}
			else if (key == "pic") {
				if (!value.empty()) LoadPicture(area.m_Thumbnail, value);
			}
			else if (key == "text") {
				if (!value.empty()) LoadText(area.m_Description, value);
			}
		}
		f.close();

		// If name is empty, fall back to project name
		if (area.m_Name.empty() && !area.m_ProjectName.empty()) {
			area.m_Name = area.m_ProjectName;
		}

		// Validate the .MAP file actually exists. We try the candidates in
		// order of decreasing specificity:
		//   1. Whatever the modder wrote in 'mapfile' (normalized)
		//   2. <mapfile> with .map appended if no extension was given
		//   3. huntdat/areas/<stem>.map (fallback derived from project name)
		std::vector<std::string> mapCandidates;
		auto normalize = [](std::string s) {
			// MEE samples use double backslashes ('huntdat\\areas\\x') as a
			// Windows-INI escape; collapse them to single forward slashes
			// so fs::path / ifstream treat them uniformly on both platforms.
			for (auto& c : s) if (c == '\\') c = '/';
			return s;
		};
		auto endsWithMap = [](const std::string& s) {
			return s.size() >= 4 &&
			       s[s.size() - 4] == '.' && s[s.size() - 3] == 'm' &&
			       s[s.size() - 2] == 'a' && s[s.size() - 1] == 'p';
		};
		if (!mapfile.empty()) {
			std::string mf = normalize(mapfile);
			mapCandidates.push_back(mf);
			if (!endsWithMap(mf)) mapCandidates.push_back(mf + ".map");
		}
		if (!area.m_ProjectName.empty()) {
			mapCandidates.push_back(areasDir + "/" + area.m_ProjectName + ".map");
		}

		bool mapFound = false;
		std::string usedMap;
		for (const auto& candidate : mapCandidates) {
			std::ifstream mf(candidate, std::ios::binary);
			if (mf.is_open()) {
				mapFound = true;
				usedMap = candidate;
				mf.close();
				break;
			}
		}

		if (!mapFound) {
			std::cout << "LoadC2Maps: '" << c2mapPath
			          << "' skipped, no .MAP file found (tried "
			          << mapCandidates.size() << " candidates)" << std::endl;
			skipped++;
			continue;
		}

		// Check for duplicate project name (script-defined entry wins)
		bool duplicate = false;
		for (const auto& existing : g_AreaInfo) {
			if (existing.m_ProjectName == area.m_ProjectName) {
				duplicate = true;
				break;
			}
		}
		if (duplicate) {
			std::cout << "LoadC2Maps: '" << c2mapPath
			          << "' skipped, project name '" << area.m_ProjectName
			          << "' already in g_AreaInfo (script entry wins)"
			          << std::endl;
			duplicates++;
			continue;
		}

		area.m_Valid = true;
		g_AreaInfo.push_back(area);
		loaded++;
		std::cout << "LoadC2Maps: added '" << area.m_Name
		          << "' (map=" << usedMap
		          << ", price=" << area.m_Price
		          << ", rank=" << area.m_Rank << ")" << std::endl;
	}

	std::cout << "LoadC2Maps: discovered=" << discovered
	          << " loaded=" << loaded
	          << " skipped=" << skipped
	          << " duplicates=" << duplicates
	          << " total areas=" << g_AreaInfo.size() << std::endl;
}


void LoadResourcesScript()
{
	FILE* file;
	char line[256];

	// Initialise some things
	g_StartCredits = 100; // Default
	g_ScriptLine = 0;
	g_AccessoryScoreMods.clear(); // reset on each script load
	g_AccessoryPrices.clear();

	// Try _MENU.TXT first (simplified menu data with prices)
	// Fall back to _res.txt if _MENU.TXT doesn't exist
	file = fopen("huntdat/_menu.txt", "r");
	if (!file) {
		file = fopen("huntdat/_res.txt", "r");
	}
	if (!file) {
		throw std::runtime_error("Can't open resources file _menu.txt or _res.txt");
		return;
	}

	try
	{
		std::cout << "Loading resources script..." << std::endl;

		while (fgets(line, 255, file))
		{
			g_ScriptLine++;

			if (line[0] == '.') break;			//endoffile EOF
			if (line[0] == '#') continue;	//comment
			if (line[0] == ';') continue;	//comment

			// Menu-relevant sections (these must appear at start of line)
			if (strstr(line, "weapons")) {
				std::cout << "Found 'weapons' section at line " << g_ScriptLine << std::endl;
				ReadWeapons(file);
				std::cout << "Loaded " << g_WeapInfo.size() << " weapons" << std::endl;
			}
			else if (strstr(line, "characters")) {
				std::cout << "Found 'characters' section at line " << g_ScriptLine << std::endl;
				ReadCharacters(file);
				std::cout << "Loaded " << g_DinoInfo.size() << " characters" << std::endl;
			}
			else if (strstr(line, "prices")) {
				std::cout << "Found 'prices' section at line " << g_ScriptLine << std::endl;
				ReadPrices(file);
				std::cout << "Loaded " << g_AreaInfo.size() << " areas" << std::endl;
			}
			else if (strstr(line, "accessories")) {
				std::cout << "Found 'accessories' section at line " << g_ScriptLine << std::endl;
				ReadAccessories(file);
			}

			// C2ME sections we don't need - skip their blocks
			// Note: these checks require the keyword at the start of the line
			// to avoid matching substrings inside other sections
			else if (strstr(line, "common") || strstr(line, "areatable") ||
			         strstr(line, "spawntable") || strstr(line, "packtable") ||
			         strstr(line, "trophytable") || strstr(line, "oldambients") ||
			         strstr(line, "corpseambients") || strstr(line, "mapambients") ||
			         strstr(line, "hunterinfo")) {
				if (strstr(line, "{")) {
					SkipNestedBlock(file, line, 255);
				}
			}
		}

		std::cout << "Finished loading resources script" << std::endl;
	}
	catch (script_error& e)
	{
		throw std::runtime_error(e.what());
	}

	fclose(file);

	// Custom-map discovery (.c2map files in HUNTDAT/AREAS/).
	// Modder-friendly feature: ship a .c2map descriptor and the menu
	// will pick it up without editing _MENU.TXT / _RES.TXT.
	// We do this AFTER the main script load so script-defined entries
	// take precedence on duplicate project names.
	LoadC2Maps();
}


void LoadResources()
{
	//TODO use this and ignore old _RES stuff

	// TODO: enable these and use them instead of GDI drawing for the trackbars
	//LoadPicture(g_TrackBar[0], "huntdat/menu/sl_bar.tga");
	//LoadPicture(g_TrackBar[1], "huntdat/menu/sl_but.tga");

	UtilInfo ui;
	size_t accIdx = 0;
	auto addUtil = [&](const char* name, const char* descFile, const char* cmd, const char* scoreKey, float scoreDefault, int32_t defaultPrice, const char* pic) {
		ui.m_Name = name;
		LoadText(ui.m_Description, descFile);
		ui.m_Command = cmd;
		ui.m_ScoreMod = LookupAccessoryScoreMod(scoreKey, scoreDefault);
		ui.m_Price = (accIdx < g_AccessoryPrices.size()) ? g_AccessoryPrices[accIdx] : defaultPrice;
		accIdx++;
		// The shop icon is optional: some accessories (e.g. tranquilizers in
		// both stock C2 and Triassic) have no dedicated image, and the
		// original game shows them with an empty icon box. Never guess a
		// different EQUIPn image - a wrong icon is worse than none.
		LoadPicture(ui.m_Thumbnail, pic);
		g_UtilInfo.push_back(ui);
		ui.m_Description.clear();
	};

	addUtil("Camouflage",  "huntdat/menu/txt/camoflag.nfo", "-camo",  "camo", 0.85f, 30,  "huntdat/menu/pics/equip1.tga");
	addUtil("Radar",       "huntdat/menu/txt/radar.nfo",    "-radar", "radar", 0.70f, 40,  "huntdat/menu/pics/equip2.tga");
	addUtil("Cover scent", "huntdat/menu/txt/scent.nfo",    "",       "scent", 0.80f, 20,  "huntdat/menu/pics/equip3.tga");
	addUtil("Double ammo", "huntdat/menu/txt/double.nfo",   "-double","double",1.0f,  50,  "huntdat/menu/pics/equip4.tga");

	// Night vision is a modernization feature and is always available, even
	// when a mod ships no nightvis.nfo or equip_nv.tga (the icon is optional;
	// the row shows with an empty icon box like other icon-less accessories).
	// A built-in description covers mods without the .nfo.
	ui.m_Name = "Night vision";
	ui.m_Description.clear();
	if (!LoadText(ui.m_Description, "huntdat/menu/txt/nightvis.nfo")) {
		ui.m_Description.push_back("Accessory : Night Vision Goggles");
		ui.m_Description.push_back("");
		ui.m_Description.push_back("Toggle a green-tinted night vision view during");
		ui.m_Description.push_back("night hunts. Press the configured key (default: N)");
		ui.m_Description.push_back("to switch it on and off. Night vision does not");
		ui.m_Description.push_back("affect your score.");
	}
	ui.m_Command = "-nightvision";
	ui.m_ScoreMod = LookupAccessoryScoreMod("nightvision", 1.0f); // neutral
	// Price rule: consume a positional acces= line ONLY when the mod priced 6
	// accessories (stock C2: camo/radar/scent/double/NV/tranq). Mods with
	// the original 5 lines (camo/radar/scent/double/tranq) keep all of them
	// for those accessories and NV falls back to its fixed default - otherwise
	// NV would steal the tranquilizer's price and tranq would lose its custom
	// price (it would fall back to 60 instead of prices[4]).
	if (g_AccessoryPrices.size() > 5) {
		ui.m_Price = g_AccessoryPrices[4]; // 5th line = NV price on 6-line mods
		accIdx++; // NV consumed a slot, so tranq reads prices[5]
	} else {
		ui.m_Price = 50; // fixed default (original feature commit)
		// Do NOT advance accIdx: tranq still reads prices[4] on 5-line mods.
	}
	LoadPicture(ui.m_Thumbnail, "huntdat/menu/pics/equip_nv.tga");
	g_UtilInfo.push_back(ui);
	ui.m_Description.clear(); // the shared buffer must not leak into the next accessory

	addUtil("Tranquilizers","huntdat/menu/txt/tranq.nfo",  "-tranq -tranquilizer", "tranq", 1.25f, 60, "huntdat/menu/pics/equip6.tga");

	// Observer info (used for info panel display, not in equipment list)
	g_ObserverInfo.m_Name = "Observer";
	LoadText(g_ObserverInfo.m_Description, "huntdat/menu/txt/observe.nfo");
	g_ObserverInfo.m_Command = "-observe -observer";
	g_ObserverInfo.m_ScoreMod = LookupAccessoryScoreMod("observe", 1.0f); // neutral


	LoadWave(g_MenuSound_Go, "huntdat/soundfx/menugo.wav");
	LoadWave(g_MenuSound_Ambient, "huntdat/soundfx/menuamb.wav");
	LoadWave(g_MenuSound_Move, "huntdat/soundfx/menumov.wav");
	LoadWave(g_MenuSound_Type, "huntdat/soundfx/type.wav");
	LoadWave(g_MenuSound_TypeGo, "huntdat/soundfx/typego.wav");
}


void ReleaseResources()
{
	g_WeapInfo.clear();
	g_DinoInfo.clear();
	g_AreaInfo.clear();
}

void TrophyLoad(Profile& profile, int pr)
{
	//PlayerProfile.RegNumber
	memset(&profile, 0, sizeof(Profile));
	profile.RegNumber = pr;

	std::stringstream fname;
	fname << "trophy" << std::setfill('0') << std::setw(2) << profile.RegNumber << ".sav";

	std::ifstream fs(fname.str(), std::ios::binary);

	if (!fs.is_open()) {
		std::stringstream ss;
		ss << "Unable to find the save file: " << fname.str();
		throw std::runtime_error(ss.str());
		return;
	}

	// Check version
	fs.seekg(0, std::ios::end);
	auto file_size = fs.tellg();

#ifdef _iceage
	if (file_size != 1664)
	{
		ShowErrorMessage("Not a compatible Carnivores: Ice Age save file!");
		fs.close();
		return;
	}
#else
	if (file_size != LegacyProfile::SaveSize)
	{
		ShowErrorMessage("Not a compatible Carnivores 2 save file!");
		fs.close();
		return;
	}
#endif

	fs.seekg(0, std::ios::beg);

#ifndef _iceage
	LegacyProfile::SaveBytes bytes{};
	fs.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
	if (!fs || !MenuProfile::LoadProfile(bytes.data(), bytes.size(), profile, g_Options)) {
		ShowErrorMessage("Not a compatible Carnivores 2 save file!");
		return;
	}
#else
	fs.read(reinterpret_cast<char*>(&profile), sizeof(Profile));

	fs.read(reinterpret_cast<char*>(&g_Options.Aggression), 4);
	fs.read(reinterpret_cast<char*>(&g_Options.Density), 4);
	fs.read(reinterpret_cast<char*>(&g_Options.Sensitivity), 4);

	fs.read(reinterpret_cast<char*>(&g_Options.Resolution), 4);
#endif
	// The old menu (StartLegacy.exe) used a hardcoded 8-entry resolution
	// table (320x240..1600x1200). Our dynamic list may differ. Convert
	// the old index to an actual resolution, then find the matching index
	// in the current list.
	{
		// Old hardcoded table (indices 0-7)
		static const struct { int w, h; } kOldRes[] = {
			{320,240}, {400,300}, {512,384}, {640,480},
			{800,600}, {1024,768}, {1280,1024}, {1600,1200}
		};
		int oldIdx = g_Options.Resolution;
		if (oldIdx >= 0 && oldIdx < 8) {
			int w = kOldRes[oldIdx].w;
			int h = kOldRes[oldIdx].h;
			// Find the matching entry in the dynamic list
			bool found = false;
			for (int r = 0; r < g_ResCount; r++) {
				if (g_ResolutionList[r].w == w && g_ResolutionList[r].h == h) {
					g_Options.Resolution = r;
					found = true;
					break;
				}
			}
			// If the old resolution isn't available (e.g. 320x240, 512x384),
			// fall back to 800x600 or the first available resolution.
			if (!found) {
				g_Options.Resolution = 0;
				for (int r = 0; r < g_ResCount; r++) {
					if (g_ResolutionList[r].w == 800 && g_ResolutionList[r].h == 600) {
						g_Options.Resolution = r;
						break;
					}
				}
			}
		}
	}
#ifdef _iceage
	// Bool fields are 1 byte in the struct but 4 bytes on disk.
	// Read into temporary int32_t to avoid adjacent-field overflow.
	{ int32_t tmp; fs.read(reinterpret_cast<char*>(&tmp), 4); g_Options.Fog = (bool)tmp; }
	fs.read(reinterpret_cast<char*>(&g_Options.Textures), 4);
	fs.read(reinterpret_cast<char*>(&g_Options.ViewRange), 4);
	g_Options.ViewRange = ClampMenuViewOpt(g_Options.ViewRange);
	{ int32_t tmp; fs.read(reinterpret_cast<char*>(&tmp), 4); g_Options.Shadows = (bool)tmp; }
	fs.read(reinterpret_cast<char*>(&g_Options.MouseSensitivity), 4);
	fs.read(reinterpret_cast<char*>(&g_Options.Brightness), 4);

	fs.read(reinterpret_cast<char*>(&g_Options.KeyMap), sizeof(TKeyMap));
	{ int32_t tmp; fs.read(reinterpret_cast<char*>(&tmp), 4); g_Options.MouseInvert = (bool)tmp; }

	{ int32_t tmp; fs.read(reinterpret_cast<char*>(&tmp), 4); g_Options.ScentMode = (bool)tmp; }
	{ int32_t tmp; fs.read(reinterpret_cast<char*>(&tmp), 4); g_Options.CamoMode = (bool)tmp; }
	{ int32_t tmp; fs.read(reinterpret_cast<char*>(&tmp), 4); g_Options.RadarMode = (bool)tmp; }
	{ int32_t tmp; fs.read(reinterpret_cast<char*>(&tmp), 4); g_Options.TranqMode = (bool)tmp; }
	fs.read(reinterpret_cast<char*>(&g_Options.AlphaColorKey), 4);

	fs.read(reinterpret_cast<char*>(&g_Options.OptSys), 4);
	fs.read(reinterpret_cast<char*>(&g_Options.SoundAPI), 4);
	fs.read(reinterpret_cast<char*>(&g_Options.RenderAPI), 4);
#endif
	g_Options.RenderAPI = NormalizeMenuRenderAPI(g_Options.RenderAPI);
	g_Options.SoundAPI = NormalizeAudioBackend(g_Options.SoundAPI);

	// FOV and other extended settings are now in config.cfg, not here.
	// Set defaults; LoadConfig() will override if the config file exists.
	g_Options.FOV = kFovDefault;
	g_Options.ViewRange = ClampMenuViewOpt(g_Options.ViewRange);
	g_Options.ObjectDetail = kObjectDetailDefault;
	g_Options.NightVisionKey = 0x4E; // Default: 'N' key

	//Temporary:
	int r = profile.Rank;
	MenuProfile::UpdateRank(profile);

	std::cout << "Profile Loaded." << std::endl;
}


void TrophySave(Profile& profile)
{
	std::stringstream fname;
	fname << "trophy" << std::setfill('0') << std::setw(2) << profile.RegNumber << ".sav";

	int r = profile.Rank;
	MenuProfile::UpdateRank(profile);

	/*
	// Taken from Carnivores 1
	if (r != TrophyRoom.Rank) {
		if (profile.Rank == RANK_ADVANCED) MenuState = 112;
		if (profile.Rank == RANK_MASTER) MenuState = 113;
	}*/

	std::ofstream fs(fname.str(), std::ios::binary | std::ios::trunc);

	if (!fs.is_open()) {
		std::cout << "Profile: Error saving trophy!" << std::endl;
		return;
	}

#ifndef _iceage
	const auto bytes = LegacyProfile::EncodeSave({MenuProfile::FromRuntime(profile),
	                                            MenuProfile::CaptureOptions(g_Options)});
	fs.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	if (!fs) {
		std::cout << "Profile: Error writing trophy!" << std::endl;
		return;
	}
#else
	fs.write(reinterpret_cast<char*>(&profile), sizeof(Profile));

	fs.write(reinterpret_cast<char*>(&g_Options.Aggression), 4);
	fs.write(reinterpret_cast<char*>(&g_Options.Density), 4);
	fs.write(reinterpret_cast<char*>(&g_Options.Sensitivity), 4);
	fs.write(reinterpret_cast<char*>(&g_Options.Resolution), 4);
	// Bool fields are 1 byte in the struct but 4 bytes on disk.
	// Write via temporary int32_t to avoid adjacent-field overflow.
	{ int32_t tmp = g_Options.Fog ? 1 : 0; fs.write(reinterpret_cast<char*>(&tmp), 4); }
	fs.write(reinterpret_cast<char*>(&g_Options.Textures), 4);
	fs.write(reinterpret_cast<char*>(&g_Options.ViewRange), 4);
	{ int32_t tmp = g_Options.Shadows ? 1 : 0; fs.write(reinterpret_cast<char*>(&tmp), 4); }
	fs.write(reinterpret_cast<char*>(&g_Options.MouseSensitivity), 4);
	fs.write(reinterpret_cast<char*>(&g_Options.Brightness), 4);
	fs.write(reinterpret_cast<char*>(&g_Options.KeyMap), sizeof(TKeyMap));
	{ int32_t tmp = g_Options.MouseInvert ? 1 : 0; fs.write(reinterpret_cast<char*>(&tmp), 4); }
	{ int32_t tmp = g_Options.ScentMode ? 1 : 0; fs.write(reinterpret_cast<char*>(&tmp), 4); }
	{ int32_t tmp = g_Options.CamoMode ? 1 : 0; fs.write(reinterpret_cast<char*>(&tmp), 4); }
	{ int32_t tmp = g_Options.RadarMode ? 1 : 0; fs.write(reinterpret_cast<char*>(&tmp), 4); }
	{ int32_t tmp = g_Options.TranqMode ? 1 : 0; fs.write(reinterpret_cast<char*>(&tmp), 4); }
	fs.write(reinterpret_cast<char*>(&g_Options.AlphaColorKey), 4);
	fs.write(reinterpret_cast<char*>(&g_Options.OptSys), 4);
	fs.write(reinterpret_cast<char*>(&g_Options.SoundAPI), 4);
	fs.write(reinterpret_cast<char*>(&g_Options.RenderAPI), 4);

#endif

	// FOV and other extended settings live in config.cfg, not here.

	std::cout << "Profile Saved." << std::endl;
}


bool ReadTGAFile(const std::string& path, TargaImage& tga)
{
	std::ifstream fs(path, std::ios::binary);

	if (!fs.is_open()) {
		std::cout << "Warning: Failed to open TGA file: " << path << std::endl;
		return false;
	}

	fs.read(reinterpret_cast<char*>(&tga.m_Header), sizeof(TARGAINFOHEADER));

	if (tga.m_Header.tgaColorMapType) {
		std::cout << "Has a color palette: " << path << std::endl;
		return false;
	}

	if (tga.m_Header.tgaImageType != TGA_IMAGETYPE_RGB) {
		std::cout << "Not an RGB image: " << path << std::endl;
		return false;
	}

	if (tga.m_Header.tgaIdentSize) {
		fs.seekg(tga.m_Header.tgaIdentSize, std::ios::cur); // Skip Ident header
	}

	if (tga.m_Data)
		delete[] tga.m_Data;

	int size = (tga.m_Header.tgaWidth * (tga.m_Header.tgaBits / 8)) * tga.m_Header.tgaHeight;
	tga.m_Data = new uint8_t[size];
	fs.read(reinterpret_cast<char*>(tga.m_Data), size);

	return true;
}


bool LoadPicture(Picture& pic, const std::string& fpath)
{
	TargaImage tga;

	if (ReadTGAFile(fpath, tga))
	{
		// Menu art is 16-bit 5551. Reject anything else rather than
		// reinterpreting the bytes (a 24/32-bit mod icon would decode
		// to garbage and the w*h uint16 loop below would overrun).
		if (tga.m_Header.tgaBits != 16) {
			std::cout << "LoadPicture: expected 16-bit TGA: " << fpath << std::endl;
			return false;
		}

		pic.m_Width = tga.m_Header.tgaWidth;
		pic.m_Height = tga.m_Header.tgaHeight;

		if (pic.m_Data)
			delete[] pic.m_Data;

		pic.m_Data = new uint16_t[pic.m_Width * pic.m_Height];
		// Menu pictures are 16-bit 5551 (alpha = bit 15). Some source TGAs
		// (e.g. equip_nv.tga) store pixels with bit 15 clear, which the menu
		// treats as fully transparent -> invisible icon. Force the alpha bit
		// so any loaded picture renders opaque like the stock equipment icons.
		const uint16_t* src = reinterpret_cast<const uint16_t*>(tga.m_Data);
		for (unsigned i = 0; i < pic.m_Width * pic.m_Height; ++i) {
			pic.m_Data[i] = static_cast<uint16_t>(src[i] | 0x8000u);
		}

		return true;
	}

	return false;
}


bool LoadText(std::vector<std::string>& txt, const std::string& path)
{
	std::ifstream tf(path);

	if (tf.is_open())
	{
		while (!tf.eof())
		{
			std::string line = "";
			std::getline(tf, line);
			txt.push_back(line);
		}
		tf.close();
		return true;
	}
	else
	{
		std::cout << "LoadText() : Unable to open text file for reading: '" << path << "'" << std::endl;
	}

	return false;
}


bool LoadWave(SoundFX& sfx, const std::string& path)
{
	std::ifstream tf(path, std::ios::binary);

	if (tf.is_open())
	{
		tf.seekg(36);

		char c[5]; c[4] = 0;

		while (true)
		{
			c[0] = tf.get();
			if (c[0] == 'd')
			{
				tf.read(&c[1], 3);
				if (!std::string(c).compare("data")) break;
				else tf.seekg(-3, std::ios::cur);
			}
		}

		tf.read(reinterpret_cast<char*>(&sfx.m_Length), 4);

		if (sfx.m_Data)
			delete[] sfx.m_Data;

		sfx.m_Data = new int16_t[sfx.m_Length / sizeof(int16_t)];
		tf.read(reinterpret_cast<char*>(sfx.m_Data), sfx.m_Length);

		tf.close();
		return true;
	}
	else
	{
		std::cout << "LoadWave() : Unable to open .wav file for reading: '" << path << "'" << std::endl;
	}

	return false;
}


void TrophyDelete(uint32_t trophy_index)
{
	std::stringstream fname;
	fname << "trophy" << std::setfill('0') << std::setw(2) << trophy_index << ".sav";
	std::filesystem::path fp = std::filesystem::current_path();
	fp /= std::filesystem::path(fname.str());
	std::cout << "Deleting trophy: \'" << fp << "\'" << std::endl;
	std::filesystem::remove(fp);
}


void Profile::New(const std::string& name, int32_t index)
{
	if (index == -1)
		index = g_ProfileIndex;

	memset(Name, 0, 128);
	memcpy(Name, name.data(), name.length());
	RegNumber = index;
	Score = g_StartCredits;
	Rank = RANK_BEGINNER;
	memset(&Last, 0, sizeof(ProfileStats));
	memset(&Total, 0, sizeof(ProfileStats));
	memset(Body, 0, sizeof(TrophyItem) * 24);
}


void Options::Default()
{
	this->Aggression = 128;
	this->Density = 128;
	this->Sensitivity = 128;
	this->Resolution = 5;
	this->DisplayMode = 2; // Borderless fullscreen (legacy menu launch default)
	this->Fog = true;
	this->Textures = 1;
	this->ViewRange = kViewOptDefault;
	this->ObjectDetail = kObjectDetailDefault;
	this->Brightness = 128;
	this->Shadows = true;
	this->MouseSensitivity = 128;
	this->FOV = kFovDefault;
	// -- Set default controls
	this->KeyMap.fkForward = 'W';
	this->KeyMap.fkBackward = 'S';
	this->KeyMap.fkSLeft = 'A';
	this->KeyMap.fkSRight = 'D';
	this->KeyMap.fkFire = VK_LBUTTON;
	this->KeyMap.fkShow = VK_RBUTTON;
	this->KeyMap.fkJump = VK_SPACE;
	this->KeyMap.fkCall = VK_MENU;
	this->KeyMap.fkBinoc = 'B';
	this->KeyMap.fkCrouch = 'C';
	this->KeyMap.fkRun = VK_LSHIFT;
	this->KeyMap.fkReload = 'R';
	this->KeyMap.fkResupply = 'T';
	this->KeyMap.fkHoldBreath = VK_LCONTROL;
	this->KeyMap.fkFiringMode = 'V';
	this->KeyMap.fkStrafe = 'G'; // Rack / Pump (also used for strafe movement by the MEE engine)
#ifdef _iceage
	this->KeyMap.fkSupply = 'O';
#endif //_iceage
	this->MouseInvert = false;
	this->ScentMode = false;
	this->CamoMode = false;
	this->RadarMode = false;
	this->TranqMode = false;
	this->AlphaColorKey = 1;
	this->OptSys = 1;
	this->SoundAPI = AUDIO_OPENALSOFT; // Default to OpenAL Soft
	this->RenderAPI = 1; // Default to OpenGL
	this->OptFpsLimit = kFpsLimitDefault;
	this->VerboseLogging = false;
	this->NightVisionKey = 0x4E; // Default: 'N' key
}


SoundFX::SoundFX() :
	m_Data(nullptr),
	m_Length(0U),
	m_Frequency(22050U)
{
}


SoundFX::SoundFX(const SoundFX& w)
{
	if (w.m_Length && w.m_Data) {
		m_Data = new int16_t[w.m_Length];
		memcpy(m_Data, w.m_Data, w.m_Length);
	}

	m_Length = w.m_Length;
	m_Frequency = w.m_Frequency;
}


SoundFX::~SoundFX()
{
	if (m_Data)
		delete[] m_Data;
}


Picture::Picture() :
	m_Width(0),
	m_Height(0),
	m_Data(nullptr)
{}


Picture::Picture(const Picture& p)
{
	m_Data = nullptr;

	if (p.m_Width && p.m_Height && p.m_Data) {
		m_Data = new uint16_t[p.m_Width * p.m_Height];
		memcpy(m_Data, p.m_Data, (p.m_Width * 2) * p.m_Height);
	}

	m_Width = p.m_Width;
	m_Height = p.m_Height;
}


Picture::~Picture()
{
	if (m_Data)
		delete[] m_Data;
}


Picture& Picture::operator= (const Picture& rhs)
{
	m_Data = nullptr;

	if (rhs.m_Width && rhs.m_Height && rhs.m_Data) {
		m_Data = new uint16_t[rhs.m_Width * rhs.m_Height];
		memcpy(m_Data, rhs.m_Data, (rhs.m_Width * 2u) * rhs.m_Height);
	}

	m_Width = rhs.m_Width;
	m_Height = rhs.m_Height;

	return *this;
}

bool Picture::IsValid() const
{
	return (m_Width > 0 && m_Height > 0 && m_Data);
}


// ================================================================
// config.cfg — text-based settings file
// ================================================================
// Format: one setting per line, "key value".
// Lines starting with '#' are comments.  Unknown keys are ignored.
// This file is the single source of truth for settings that are
// not part of the legacy binary trophy format.
// ================================================================

// Resolve config.cfg relative to the EXE directory first, falling back
// to the current working directory.  Both the Menu and the game engine
// use the same strategy so they share the same file regardless of which
// directory each EXE is launched from.
static std::string GetConfigPath()
{
	char mod[MAX_PATH];
	DWORD len = GetModuleFileNameA(nullptr, mod, sizeof(mod));
	if (len > 0 && len < sizeof(mod)) {
		char* sep = strrchr(mod, '\\');
		if (sep) {
			*(sep + 1) = '\0';
			strcat_s(mod, sizeof(mod), "config.cfg");
			if (GetFileAttributesA(mod) != INVALID_FILE_ATTRIBUTES)
				return std::string(mod);
		}
	}
	return "config.cfg";
}

static std::string GetConfigWritePath()
{
	char mod[MAX_PATH];
	DWORD len = GetModuleFileNameA(nullptr, mod, sizeof(mod));
	if (len > 0 && len < sizeof(mod)) {
		char* sep = strrchr(mod, '\\');
		if (sep) {
			*(sep + 1) = '\0';
			return std::string(mod) + "config.cfg";
		}
	}
	return "config.cfg";
}

// Write the current settings to config.cfg (EXE-directory based, shared
// with the game engine).
void SaveConfig()
{
	std::string configPath = GetConfigWritePath();

	// Snapshot lines this build does not own (user comments, game-side
	// envN_* audio keys, future keys) so the rewrite preserves them
	// verbatim, in place. Without this, every save silently deletes hand
	// edits and the render exe's audio tweaks.
	std::vector<std::string> oldLines;
	{
		std::ifstream in(configPath);
		std::string l;
		while (std::getline(in, l)) oldLines.push_back(l);
	}

	// Fresh renderings of every key this build owns, in canonical order.
	// Conditional keys (resolution, hunt_*) are present only when valid —
	// a missing entry means "leave any existing line alone, append nothing".
	std::vector<std::pair<std::string, std::string>> fresh;
	fresh.emplace_back("renderer", std::to_string(g_Options.RenderAPI));
	fresh.emplace_back("fov", std::to_string(g_Options.FOV));
	fresh.emplace_back("object_detail", std::to_string(g_Options.ObjectDetail));
	fresh.emplace_back("fps_limit", std::to_string(g_Options.OptFpsLimit));
	fresh.emplace_back("verbose_logging", g_Options.VerboseLogging ? "1" : "0");
	fresh.emplace_back("nightvision_key", std::to_string(g_Options.NightVisionKey));

	// Persist the selected resolution as WxH so the render exe honours it
	// even though the legacy per-profile index (trophy0N.sav) is fragile
	// across differently-ordered mode lists.
	if (g_Options.Resolution >= 0 && g_Options.Resolution < g_ResCount) {
		std::string res = std::to_string(g_ResolutionList[g_Options.Resolution].w)
			+ "x" + std::to_string(g_ResolutionList[g_Options.Resolution].h);
		fresh.emplace_back("resolution", res);
	}

	// Persist the display mode (0=windowed, 1=exclusive fullscreen,
	// 2=borderless fullscreen). The render exe applies it on launch.
	int dm = g_Options.DisplayMode;
	if (dm < 0 || dm > 2) dm = 2;
	fresh.emplace_back("display_mode", std::to_string(dm));

	// Remember the last hunt setup (captured by Menu.cpp from the MenuHunt
	// lists). Guarded: the lists only exist after the first hunt-screen
	// visit, so a fresh boot or options-only session never clobbers the file
	// with empty defaults.
	if (g_HasSavedHunt) {
		fresh.emplace_back("hunt_area", g_SavedHuntArea);
		fresh.emplace_back("hunt_dinos", std::to_string(g_SavedHuntDinos));
		fresh.emplace_back("hunt_weapons", std::to_string(g_SavedHuntWeapons));
		fresh.emplace_back("hunt_utils", std::to_string(g_SavedHuntUtils));
		fresh.emplace_back("hunt_time", std::to_string(g_SavedHuntTime));
	}

	std::ofstream fs(configPath, std::ios::trunc);
	if (!fs.is_open()) {
		std::cout << "Config: could not write " << configPath << std::endl;
		return;
	}

	if (oldLines.empty()) {
		// First run: canonical file, exactly as before.
		fs << "# Carnivores 2 Modder's Engine configuration\n";
		fs << "# Edit by hand if needed — values are validated on load.\n";
		fs << "\n";
		for (const auto& kv : fresh) fs << kv.first << " " << kv.second << "\n";
	} else {
		// Merge: owned keys get fresh values in place (comments and
		// unowned keys pass through verbatim, so hand edits and the
		// render exe's audio keys survive); brand-new keys append once.
		// A stale duplicate of an owned key is dropped — the parser lets
		// the last line win, so keeping it would override the fresh value.
		std::set<std::string> written;
		for (const auto& l : oldLines) {
			size_t b = l.find_first_not_of(" \t");
			std::string key;
			if (b != std::string::npos && l[b] != '#') {
				size_t e = l.find_first_of(" \t", b);
				key = l.substr(b, e == std::string::npos ? e : e - b);
			}
			auto it = fresh.end();
			for (auto f = fresh.begin(); f != fresh.end(); ++f)
				if (f->first == key) { it = f; break; }
			if (it == fresh.end()) { fs << l << "\n"; continue; }
			if (written.count(key)) continue;
			fs << it->first << " " << it->second << "\n";
			written.insert(key);
		}
		for (const auto& kv : fresh)
			if (!written.count(kv.first)) fs << kv.first << " " << kv.second << "\n";
	}

	std::cout << "Config Saved (" << configPath << ")." << std::endl;
}

// Parse a single "key value" line.  Returns true if the key was recognised.
static bool ParseConfigLine(const std::string& line)
{
	std::istringstream iss(line);
	std::string key;
	if (!(iss >> key)) return false;
	if (key[0] == '#') return false;  // comment

	if (key == "renderer") {
		int v;
		if (iss >> v) {
			// 0=Software, 1=OpenGL. Legacy Direct3D values are normalized
			// to OpenGL so old configs do not launch v_d3d.ren.
			g_Options.RenderAPI = NormalizeMenuRenderAPI(v);
		}
		return true;
	}

	if (key == "fov") {
		int v;
		if (iss >> v) {
			if (v < kFovMin) v = kFovMin;
			if (v > kFovMax) v = kFovMax;
			g_Options.FOV = v;
		}
		return true;
	}

	if (key == "object_detail") {
		int v;
		if (iss >> v) {
			v = ClampMenuObjectDetail(v);
			g_Options.ObjectDetail = v;
		}
		return true;
	}

	if (key == "fps_limit") {
		int v;
		if (iss >> v) {
			if (v < 0) v = 0;
			if (v >= kFpsLimitCount) v = kFpsLimitDefault;
			g_Options.OptFpsLimit = v;
		}
		return true;
	}

	if (key == "verbose_logging") {
		int v;
		if (iss >> v) {
			g_Options.VerboseLogging = (v != 0);
		}
		return true;
	}

	if (key == "nightvision_key") {
		int v;
		if (iss >> v) {
			g_Options.NightVisionKey = v;
		}
		return true;
	}

	if (key == "resolution") {
		int w = 0, h = 0;
		char x = 0;
		if (iss >> w >> x >> h && (x == 'x' || x == 'X') && w > 0 && h > 0) {
			// Translate WxH back to an index in the current mode list.
			for (int r = 0; r < g_ResCount; r++) {
				if (g_ResolutionList[r].w == w && g_ResolutionList[r].h == h) {
					g_Options.Resolution = r;
					break;
				}
			}
		}
		return true;
	}

	if (key == "display_mode") {
		int v;
		if (iss >> v) {
			if (v >= 0 && v <= 2)
				g_Options.DisplayMode = v;
		}
		return true;
	}

	// Last hunt setup (see SaveConfig). Everything is validated again
	// against the live lists on restore, so stale/modded values can only
	// fall back to defaults, never select out of range.
	if (key == "hunt_area") {
		std::string v;
		if (iss >> v && v.size() < 128) {
			g_SavedHuntArea = v;
			g_HasSavedHunt = true;
		}
		return true;
	}
	if (key == "hunt_dinos" || key == "hunt_weapons" || key == "hunt_utils") {
		unsigned long long v;
		if (iss >> v) {
			if (key == "hunt_dinos") g_SavedHuntDinos = v;
			else if (key == "hunt_weapons") g_SavedHuntWeapons = v;
			else g_SavedHuntUtils = v;
			g_HasSavedHunt = true;
		}
		return true;
	}
	if (key == "hunt_time") {
		int v;
		if (iss >> v && v >= HUNT_DAWN && v <= HUNT_NIGHT) {
			g_SavedHuntTime = v;
			g_HasSavedHunt = true;
		}
		return true;
	}

	// Unknown key — ignore gracefully (forward-compat with newer configs)
	return false;
}

// Read config.cfg and override g_Options fields.
// If the file does not exist, write defaults (first-run migration).
void LoadConfig()
{
	std::string configPath = GetConfigPath();
	std::ifstream fs(configPath);
	if (!fs.is_open()) {
		std::cout << "Config: " << configPath << " not found — writing defaults." << std::endl;
		SaveConfig();
		return;
	}

	std::string whole;
	size_t nulBytes = 0;
	if (!ReadConfigText(fs, whole, nulBytes)) {
		std::cout << "Config: unreadable or unsupported encoding; using defaults." << std::endl;
		return;
	}
	if (nulBytes) {
		std::cout << "Config: recovered " << nulBytes << " NUL padding bytes; please resave config.cfg." << std::endl;
	}
	std::string line;
	std::istringstream iss(whole);
	while (std::getline(iss, line)) {
		ParseConfigLine(line);
	}

	std::cout << "Config Loaded (" << configPath << ")." << std::endl;
}
