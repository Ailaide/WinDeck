#include "overlay/name_entry_queue.h"

#include <catch2/catch_test_macros.hpp>

using windeck::overlay::NameEntryQueue;

namespace {

void type(NameEntryQueue& q, const wchar_t* s) {
    for (; *s; ++s) q.append(*s);
}

} // namespace

TEST_CASE("An accepted name is taken once, even after a later cancel", "[name_entry]") {
    NameEntryQueue q;
    q.begin();
    type(q, L"Shooter");
    CHECK_FALSE(q.take().has_value());   // still open

    q.accept();
    q.cancel();                           // the menu closes straight after Enter
    auto o = q.take();
    REQUIRE(o.has_value());
    CHECK(o->commit);
    CHECK(o->name == L"Shooter");
    CHECK_FALSE(q.take().has_value());   // runs once
}

TEST_CASE("A cancelled entry never commits", "[name_entry]") {
    NameEntryQueue q;
    q.begin();
    type(q, L"Late");
    q.cancel();
    q.accept();                           // Enter after Escape is ignored
    auto o = q.take();
    REQUIRE(o.has_value());
    CHECK_FALSE(o->commit);
    CHECK(o->name.empty());
}

TEST_CASE("Enter accepts the text as it was when the key arrived", "[name_entry]") {
    NameEntryQueue q;
    q.begin();
    type(q, L"Race");
    q.accept();
    type(q, L"r");                        // typing after Enter changes nothing
    q.backspace();
    q.backspace();
    auto o = q.take();
    REQUIRE(o.has_value());
    CHECK(o->name == L"Race");
}

TEST_CASE("A new entry never sees the text or decision of an earlier one", "[name_entry]") {
    NameEntryQueue q;
    q.begin();
    type(q, L"Old");
    q.cancel();
    REQUIRE(q.take().has_value());

    q.accept();                           // no entry open: ignored
    type(q, L"x");                        // no entry open: ignored
    CHECK_FALSE(q.take().has_value());

    q.begin();
    CHECK(q.text().empty());
    type(q, L"New");
    q.accept();
    auto o = q.take();
    REQUIRE(o.has_value());
    CHECK(o->commit);
    CHECK(o->name == L"New");
}

TEST_CASE("Decisions before any entry are ignored", "[name_entry]") {
    NameEntryQueue q;
    q.accept();
    q.cancel();
    CHECK_FALSE(q.take().has_value());
}

TEST_CASE("Typing stops at the length limit and backspace edits", "[name_entry]") {
    NameEntryQueue q;
    q.begin();
    for (size_t i = 0; i < NameEntryQueue::kMaxLength + 5; i++) q.append(L'a');
    CHECK(q.text().size() == NameEntryQueue::kMaxLength);
    q.backspace();
    CHECK(q.text().size() == NameEntryQueue::kMaxLength - 1);

    q.begin();
    q.backspace();                        // empty text: nothing to remove
    CHECK(q.text().empty());
}
