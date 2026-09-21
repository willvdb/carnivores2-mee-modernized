#pragma once
// PRIVATE catalog handle representation shared by the pure parser and the
// filesystem projection. Never included from public headers.
#include "c2/frontend/catalog.hpp"
#include "json_compat.hpp"
namespace c2::frontend::catalog {
struct Node::Impl { std::shared_ptr<const compat::Value> value; };
struct Script::Impl {
    std::shared_ptr<const compat::Value> value;
    std::string bytes, hash;
};
struct Access {
    static Node node(std::shared_ptr<const compat::Value> value) {
        auto p = std::make_shared<Node::Impl>(); p->value = std::move(value);
        return Node(std::move(p));
    }
    static const std::shared_ptr<const compat::Value>& value(const Script& script) { return script.impl_->value; }
};
// Retained complete projection value (the Python project result), for sibling
// TUs that evaluate over it without serializing and reparsing exported JSON.
const std::shared_ptr<const compat::Value>& projection_value(const Projection&);
}
