#include "c2/frontend/gui/loop.hpp"
namespace c2::frontend::gui {
bool HuntLoop::select(play_loop::Association a) {
    if (busy()) return false;
    catalog_.reset(); association_ = std::move(a); session_.reset(); preview_.reset();
    state_ = LoopState::selection; details_.clear(); error_.clear(); ++version_; return true;
}
bool HuntLoop::loadout(planning::Selection s) {
    if (busy()) return false;
    selection_ = std::move(s); session_.reset(); preview_.reset(); state_ = LoopState::selection;
    ++version_; return true;
}
bool HuntLoop::inspect_session(std::u32string id) {
    if (busy() || id.empty()) return false;
    session_ = play_loop::Session{}; session_->id = std::move(id);
    preview_.reset(); state_ = LoopState::selection; ++version_; return true;
}
bool HuntLoop::can(Operation op) const {
    if (busy()) return false;
    switch (op) {
    case Operation::catalog: case Operation::plan: return association_.has_value();
    case Operation::prepare: return writable_ && association_ && (state_ == LoopState::validated || state_ == LoopState::accepted || state_ == LoopState::declined);
    case Operation::run: return writable_ && session_ && state_ == LoopState::prepared;
    case Operation::inspect: return session_.has_value();
    case Operation::preview: return session_ && session_->generation.has_value();
    case Operation::accept: return writable_ && state_ == LoopState::eligible && preview_ && preview_->allowed &&
        !preview_->candidate_sha256.empty() && !preview_->expected_generation.empty() && !preview_->session_id.empty();
    case Operation::recover: case Operation::recover_acceptance: return writable_ && session_.has_value();
    case Operation::upgrade: return writable_;
    }
    return false;
}
std::optional<LoopRequest> HuntLoop::begin(Operation op) {
    if (!can(op)) return {};
    LoopRequest r; r.id = next_++; r.operation = op; r.selection = selection_;
    if (association_) r.association = association_->id;
    if (session_) { r.session = session_->id; r.generation = session_->generation.value_or(U""); }
    if (op == Operation::accept) r.preview = *preview_;
    if (op != Operation::accept) preview_.reset();
    switch (op) {
    case Operation::catalog: case Operation::plan: state_ = LoopState::validating; break;
    case Operation::prepare: session_.reset(); state_ = LoopState::preparing; break;
    case Operation::run: state_ = LoopState::running; break;
    case Operation::inspect: state_ = LoopState::inspecting; break;
    case Operation::preview: state_ = LoopState::previewing; break;
    case Operation::accept: state_ = LoopState::accepting; break;
    default: state_ = LoopState::recovering;
    }
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    pending_ = r; error_.clear(); ++version_; return r;
}
bool HuntLoop::complete(std::uint64_t id, LoopResult result) {
    if (!pending_ || pending_->id != id) return false;
    auto op = pending_->operation; pending_.reset(); ++version_;
    if (!result.error.empty()) { error_ = std::move(result.error); state_ = LoopState::error; return true; }
    if (auto c = std::get_if<catalog::Projection>(&result.value)) {
        catalog_ = *c; details_ = c->export_json(); state_ = LoopState::selection;
    } else if (auto p = std::get_if<play_loop::Plan>(&result.value)) {
        details_ = p->details; state_ = LoopState::validated;
    } else if (auto s = std::get_if<play_loop::Session>(&result.value)) {
        session_ = *s; details_ = s->details;
        state_ = op == Operation::prepare ? LoopState::prepared :
                 op == Operation::run ? LoopState::returned : LoopState::reviewing;
    } else if (auto p = std::get_if<play_loop::Preview>(&result.value)) {
        preview_ = *p; details_ = p->details;
        state_ = p->allowed ? LoopState::eligible : LoopState::blocked;
    } else if (auto r = std::get_if<play_loop::Receipt>(&result.value)) {
        details_ = r->details;
        if (op == Operation::accept && (r->result == U"accepted" || r->result == U"already-accepted")) {
            if (association_) association_->current_generation = r->current_generation;
            state_ = LoopState::accepted; session_.reset(); preview_.reset();
        } else state_ = LoopState::reviewing;
    } else if (auto e = std::get_if<play_loop::Evidence>(&result.value)) {
        details_ = e->details; state_ = LoopState::selection;
    } else { error_ = "operation returned no observation"; state_ = LoopState::error; }
    return true;
}
void HuntLoop::cancel() { if (busy()) { cancel_->store(true); ++version_; } }
void HuntLoop::decline() {
    if (busy()) return;
    preview_.reset(); state_ = LoopState::declined; ++version_;
}
LoopResult execute_loop(const play_loop::Client& client, const LoopRequest& r,
                        const play_loop::Authorization& auth, const std::atomic<bool>& cancel) {
    try {
        switch (r.operation) {
        case Operation::catalog: return {client.catalog(r.association),{}};
        case Operation::plan: return {client.plan(r.association,r.selection),{}};
        case Operation::prepare: return {client.prepare(r.association,r.selection,auth),{}};
        case Operation::run: {
            client.run(r.session,auth,cancel);
            return {client.reconcile(r.session),{}};
        }
        case Operation::inspect: return {client.inspect(r.session),{}};
        case Operation::preview: return {client.preview(r.session,r.generation),{}};
        case Operation::accept: return {client.accept(r.preview),{}};
        case Operation::recover: return {client.recover(r.session),{}};
        case Operation::recover_acceptance: return {client.recover_acceptance(r.session),{}};
        case Operation::upgrade: return {client.upgrade(),{}};
        }
    } catch (const std::exception& e) { return {{},e.what()}; }
    return {{},"unknown operation"};
}
}
