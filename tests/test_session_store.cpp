// ClawLite — 会话存储测试
// TODO: B 同学补充测试用例

#include "memory/session_store.h"
#include "test_helpers.h"
#include <iostream>

using namespace clawlite;

void testSessionCrud() {
    SessionStore store;

    store.createSession("agent:main:cli:user:default");
    TEST_ASSERT(store.hasSession("agent:main:cli:user:default"));
    TEST_ASSERT(store.sessionCount() == 1);

    store.appendMessage("agent:main:cli:user:default", Message::user("Hello"));
    store.appendMessage("agent:main:cli:user:default", Message::assistant("Hi!"));

    auto history = store.getHistory("agent:main:cli:user:default", 10);
    TEST_ASSERT(history.size() == 2);
    if (history.size() >= 2) {
        TEST_ASSERT(history[0].role == Role::User);
        TEST_ASSERT(history[1].role == Role::Assistant);
    }

    store.clear();
    std::cout << "  [PASS] testSessionCrud\n";
}

void testParseSessionKey() {
    auto info = SessionStore::parseSessionKey("agent:main:cli:user:default");
    TEST_ASSERT(info.agentId == "main");
    TEST_ASSERT(info.channel == "cli");
    TEST_ASSERT(info.peerKind == "user");
    TEST_ASSERT(info.peerId == "default");
    std::cout << "  [PASS] testParseSessionKey\n";
}

void testBuildSessionKey() {
    std::string key = SessionStore::buildSessionKey("main", "cli", "user", "default");
    TEST_ASSERT(key == "agent:main:cli:user:default");
    std::cout << "  [PASS] testBuildSessionKey\n";
}

void testListByAgent() {
    SessionStore store;
    store.createSession("agent:main:cli:user:u1");
    store.createSession("agent:main:cli:user:u2");
    store.createSession("agent:other:cli:user:u1");

    auto sessions = store.listSessionsByAgent("main");
    TEST_ASSERT(sessions.size() == 2);

    store.clear();
    std::cout << "  [PASS] testListByAgent\n";
}

int run_session_store_tests() {
    std::cout << "Session Store Tests:\n";
    RESET_FAILURES();
    testSessionCrud();
    testParseSessionKey();
    testBuildSessionKey();
    testListByAgent();
    int f = GET_FAILURES();
    if (f == 0) std::cout << "All session store tests passed.\n";
    else std::cout << f << " session store test(s) failed.\n";
    return f;
}
