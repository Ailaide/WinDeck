#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>

namespace windeck::overlay {

// Typing a profile name: the text and its outcome, handed from the thread that
// sees the keys (the overlay's keyboard hook) to the input thread, which owns
// every profile change.
//
// One mutex guards the text and the decision, so Enter always accepts the text
// typed in the entry it ends, exactly as it was when the key arrived. The first
// decision of an entry wins (Enter accepts, Escape or B cancels); later
// decisions and typing are ignored. take() hands the decision over exactly once.
// So an accepted name survives the menu closing straight after, and a cancelled
// entry never creates a profile.
class NameEntryQueue {
public:
    static constexpr size_t kMaxLength = 40;

    struct Outcome {
        bool commit = false;
        std::wstring name;   // the typed name when commit
    };

    // Start a new entry with empty text.
    void begin() {
        std::lock_guard lock(mtx_);
        state_ = State::Open;
        text_.clear();
        outcome_ = {};
    }

    // Typing, while the entry is open and undecided.
    void append(wchar_t c) {
        std::lock_guard lock(mtx_);
        if (state_ == State::Open && text_.size() < kMaxLength) text_.push_back(c);
    }
    void backspace() {
        std::lock_guard lock(mtx_);
        if (state_ == State::Open && !text_.empty()) text_.pop_back();
    }

    // The text typed so far, for drawing.
    std::wstring text() const {
        std::lock_guard lock(mtx_);
        return text_;
    }

    // Enter: accept the text typed so far, if the entry is open and undecided.
    void accept() {
        std::lock_guard lock(mtx_);
        if (state_ != State::Open) return;
        outcome_ = {true, text_};
        state_ = State::Decided;
    }

    // Escape or B: cancel, if the entry is open and undecided.
    void cancel() {
        std::lock_guard lock(mtx_);
        if (state_ != State::Open) return;
        outcome_ = {false, {}};
        state_ = State::Decided;
    }

    // The entry's decision, once; empty while undecided or after it was taken.
    std::optional<Outcome> take() {
        std::lock_guard lock(mtx_);
        if (state_ != State::Decided) return std::nullopt;
        state_ = State::Done;
        return std::move(outcome_);
    }

private:
    enum class State { Idle, Open, Decided, Done };

    mutable std::mutex mtx_;
    State state_ = State::Idle;
    std::wstring text_;
    Outcome outcome_;
};

} // namespace windeck::overlay
