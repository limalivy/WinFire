//
//  状态机测试，对应 FireInputController.swift 的核心路径
//
#include "test_util.h"
#include "fire/input_engine.h"

using namespace fire;

static std::string engine_db_path() { return "test_engine.sqlite"; }

static void seed_engine() {
    build_test_db(engine_db_path(), {
        // 单字/词
        {"a", "工", "wb", "a"},
        {"aa", "式", "wb", "aa"},
        {"aaa", "工", "wb", "aaa"},
        {"aaaa", "工", "wb", "aaaa"},
        {"aaad", "工期", "wb", "aaad"},
        {"aaad", "工地", "wb", "aaad"},  // 让 aaad 有多个候选，验证输入第4码后保持组字态
        {"s", "王", "wb", "s"},
        {"ss", "林", "wb", "ss"},
        {"d", "大", "wb", "d"},
        {"f", "土", "wb", "f"},
        {"dd", "石", "wb", "dd"},
        // 52 顶：前2码 gg=五, 后2码 hh=目
        {"gg", "五", "wb", "gg"},
        {"hh", "目", "wb", "hh"},
        {"gghh", "瞐", "wb", "gghh"},
        // 53 顶：前3码 www=人, 第4码 f=土
        {"www", "人", "wb", "www"},
        {"wwwf", "�突", "wb", "wwwf"},
        // 反查
        {"ss", "林", "py", "lin"},
    });
}

static KeyEvent alpha(const std::string& s) {
    KeyEvent e;
    e.text = s;
    return e;
}
static KeyEvent digit(const std::string& s) {
    KeyEvent e;
    e.text = s;
    return e;
}
static KeyEvent space() {
    KeyEvent e;
    e.special = SpecialKey::Space;
    return e;
}
static KeyEvent backspace() {
    KeyEvent e;
    e.special = SpecialKey::Backspace;
    return e;
}
static KeyEvent enter() {
    KeyEvent e;
    e.special = SpecialKey::Enter;
    return e;
}
static KeyEvent esc() {
    KeyEvent e;
    e.special = SpecialKey::Escape;
    return e;
}

static Config make_cfg() {
    Config cfg;
    cfg.db_path = engine_db_path();
    cfg.code_mode = CodeMode::Wubi;
    cfg.candidate_count = 5;
    return cfg;
}

TEST_CASE(engine_alpha_input_builds_original) {
    seed_engine();
    Config cfg = make_cfg();
    DictManager dm(cfg);
    FakeClient client;
    InputEngine eng(cfg, dm, client);

    CHECK(eng.handle_key(alpha("a")));
    CHECK_STR_EQ(eng.original_string(), "a");
    CHECK(eng.handle_key(alpha("a")));
    CHECK_STR_EQ(eng.original_string(), "aa");
    CHECK(client.candidates_visible);
    CHECK(eng.candidates().size() >= 1);
}

TEST_CASE(engine_space_commits_first_candidate) {
    seed_engine();
    Config cfg = make_cfg();
    DictManager dm(cfg);
    FakeClient client;
    InputEngine eng(cfg, dm, client);

    eng.handle_key(alpha("a"));
    eng.handle_key(alpha("a"));  // aa -> 式
    CHECK(eng.handle_key(space()));
    CHECK_STR_EQ(client.last_insert, "式");
    // 上屏后清空
    CHECK_STR_EQ(eng.original_string(), "");
    CHECK(!client.candidates_visible);
}

TEST_CASE(engine_number_selects_candidate) {
    seed_engine();
    Config cfg = make_cfg();
    DictManager dm(cfg);
    FakeClient client;
    InputEngine eng(cfg, dm, client);

    eng.handle_key(alpha("a"));  // a -> 候选含 工/式/工期...
    size_t n = eng.candidates().size();
    CHECK(n >= 1);
    std::string want = eng.candidates()[0].text;
    CHECK(eng.handle_key(digit("1")));
    CHECK_STR_EQ(client.last_insert, want);
    CHECK_STR_EQ(eng.original_string(), "");
}

TEST_CASE(engine_backspace_deletes) {
    seed_engine();
    Config cfg = make_cfg();
    DictManager dm(cfg);
    FakeClient client;
    InputEngine eng(cfg, dm, client);

    eng.handle_key(alpha("a"));
    eng.handle_key(alpha("a"));
    CHECK_STR_EQ(eng.original_string(), "aa");
    CHECK(eng.handle_key(backspace()));
    CHECK_STR_EQ(eng.original_string(), "a");
    CHECK(eng.handle_key(backspace()));
    CHECK_STR_EQ(eng.original_string(), "");
    // 空串时 backspace 不消费
    CHECK(!eng.handle_key(backspace()));
}

TEST_CASE(engine_enter_commits_original) {
    seed_engine();
    Config cfg = make_cfg();
    DictManager dm(cfg);
    FakeClient client;
    InputEngine eng(cfg, dm, client);

    eng.handle_key(alpha("a"));
    eng.handle_key(alpha("b"));  // b 非字母? b是字母, original=ab
    CHECK(eng.handle_key(enter()));
    CHECK_STR_EQ(client.last_insert, "ab");
    CHECK_STR_EQ(eng.original_string(), "");
}

TEST_CASE(engine_esc_clears) {
    seed_engine();
    Config cfg = make_cfg();
    DictManager dm(cfg);
    FakeClient client;
    InputEngine eng(cfg, dm, client);

    eng.handle_key(alpha("a"));
    eng.handle_key(alpha("a"));
    CHECK(eng.handle_key(esc()));
    CHECK_STR_EQ(eng.original_string(), "");
    CHECK_STR_EQ(client.last_insert, "");  // esc 不上屏
}

TEST_CASE(engine_en_mode_passthrough) {
    seed_engine();
    Config cfg = make_cfg();
    DictManager dm(cfg);
    FakeClient client;
    InputEngine eng(cfg, dm, client);
    eng.set_input_mode(InputMode::EnUS, false);
    // 英文模式：字母不被消费
    CHECK(!eng.handle_key(alpha("a")));
    CHECK_STR_EQ(eng.original_string(), "");
}

TEST_CASE(engine_punctuation_commit) {
    seed_engine();
    Config cfg = make_cfg();
    cfg.punctuation_mode = PunctuationMode::ZhHans;
    cfg.enable_punctuation_commit = true;
    DictManager dm(cfg);
    FakeClient client;
    InputEngine eng(cfg, dm, client);

    eng.handle_key(alpha("a"));
    eng.handle_key(alpha("a"));  // aa -> 式
    std::string first = eng.candidates().front().text;
    // 输入逗号：先上屏首候选，再输出中文逗号
    KeyEvent comma = alpha(",");
    CHECK(eng.handle_key(comma));
    CHECK_STR_EQ(client.last_insert, first + std::string("，"));
}

TEST_CASE(engine_reverse_lookup_flow) {
    seed_engine();
    Config cfg = make_cfg();
    cfg.z_key_query = true;
    DictManager dm(cfg);
    FakeClient client;
    InputEngine eng(cfg, dm, client);

    CHECK(eng.handle_key(alpha("`")));
    CHECK_STR_EQ(eng.original_string(), "`");
    eng.handle_key(alpha("l"));
    eng.handle_key(alpha("i"));
    eng.handle_key(alpha("n"));  // `lin
    CHECK_STR_EQ(eng.original_string(), "`lin");
    CHECK(eng.candidates().size() >= 1);
    CHECK_STR_EQ(eng.candidates().front().text, "林");
}

TEST_CASE(engine_wubi52_ding_combo_on_space) {
    seed_engine();
    Config cfg = make_cfg();
    cfg.wubi_ding_mode = WubiDingMode::Ding52;
    DictManager dm(cfg);
    FakeClient client;
    InputEngine eng(cfg, dm, client);

    eng.handle_key(alpha("g"));
    eng.handle_key(alpha("g"));
    eng.handle_key(alpha("h"));
    eng.handle_key(alpha("h"));  // gghh
    CHECK_STR_EQ(eng.original_string(), "gghh");
    // 首候选应为 2+2 组合 "五目"
    CHECK_STR_EQ(eng.candidates().front().text, "五目");
    // 空格上屏组合
    CHECK(eng.handle_key(space()));
    CHECK_STR_EQ(client.last_insert, "五目");
}

TEST_CASE(engine_wubi53_ding_combo_shown) {
    seed_engine();
    Config cfg = make_cfg();
    cfg.wubi_ding_mode = WubiDingMode::Ding53;
    DictManager dm(cfg);
    FakeClient client;
    InputEngine eng(cfg, dm, client);

    eng.handle_key(alpha("w"));
    eng.handle_key(alpha("w"));
    eng.handle_key(alpha("w"));
    eng.handle_key(alpha("f"));  // wwwf
    CHECK_STR_EQ(eng.original_string(), "wwwf");
    // 首候选应为 3+1 组合 "人土"
    CHECK_STR_EQ(eng.candidates().front().text, "人土");
}

TEST_CASE(engine_wubi35_ding_space_pending) {
    seed_engine();
    Config cfg = make_cfg();
    cfg.wubi_ding_mode = WubiDingMode::Ding35;
    DictManager dm(cfg);
    FakeClient client;
    InputEngine eng(cfg, dm, client);

    eng.handle_key(alpha("a"));
    eng.handle_key(alpha("a"));
    eng.handle_key(alpha("a"));  // aaa (3码)
    CHECK_STR_EQ(eng.original_string(), "aaa");
    // 第一次空格：进入 pending，展示下划线占位，不上屏
    CHECK(eng.handle_key(space()));
    CHECK_STR_EQ(client.last_insert, "");
    CHECK_STR_EQ(eng.original_string(), "aaa");
    // 继续输入第4码 d -> aaad
    CHECK(eng.handle_key(alpha("d")));
    CHECK_STR_EQ(eng.original_string(), "aaad");
}
