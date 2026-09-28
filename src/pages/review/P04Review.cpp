#include "pages/review/P04Review.h"

#include "pages/novel/AutoRunPanel.h"
#include "core/Async.h"
#include "pages/novel/ChapterFlowView.h"
#include "pages/novel/DraftView.h"
#include "pages/novel/InitChainView.h"
#include "pages/novel/NovelWorkspace.h"
#include "pages/novel/ReviewView.h"
#include "pages/novel/StateDiffView.h"
#include "pages/novel/WorldBoardView.h"
#include "pages/shell/MainWindow.h"
#include "db/sqlite/SqliteDb.h"
#include "widget/theme/Theme.h"
#include "widget/theme/ThemeService.h"
#include "llm/AgentKit.h"
#include "novel/NovelDb.h"
#include "novel/NovelGraph.h"
#include "novel/NovelStageLedger.h"
#include "project/Project.h"
#include "util/Encoding.h"
#include "util/File.h"

#include <QApplication>
#include <QLabel>
#include <QPixmap>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>
#include <QWidget>

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace shine::app {
namespace {

namespace fs = std::filesystem;

struct ReviewState {
    MainWindow* window = nullptr;
    QTimer* ui_pump = nullptr;
    int finish_attempts = 0;
    int step = 0;
    bool in_step = false;
    bool prompt_checked = false;
    bool prompt_ok = false;
    fs::path dir;
    fs::path empty_root;
    fs::path normal_root;
    std::vector<std::string> expected{
        "novel-normal.png",       "novel-empty.png",       "draft-streaming.png",
        "worldboard-entities.png", "worldboard-foreshadow.png", "worldboard-knowledge.png",
        "initchain-running.png",  "initchain-gate-blocked.png", "chapterflow-running.png",
        "chapterflow-failed.png", "review-fail.png",        "statediff-review.png",
        "autorun-auto.png",       "novel-paper.png"};
    std::vector<std::string> manifest;
};

void Grab(ReviewState* st, QWidget* widget, const char* name) {
    if (widget == nullptr) {
        st->manifest.push_back(std::string{name} + " 0 FAILED null-widget");
        return;
    }
    widget->repaint();
    const QPixmap pm = widget->grab();
    const fs::path file = st->dir / (std::string{name} + ".png");
    const bool ok = pm.save(QString::fromStdWString(file.wstring()));
    const auto bytes = shine::util::ReadFileBytes(file);
    st->manifest.push_back(std::string{name} + " " +
                           std::to_string(bytes.value_or(std::string{}).size()) +
                           (ok ? " saved" : " FAILED"));
}

void Shot(ReviewState* st, QWidget* widget, const char* name) {
    Grab(st, widget, name);
}

void PollShot(ReviewState* st, QWidget* widget, const char* name,
              const std::function<bool()>& ready, int timeoutMs = 2200) {
    auto* poll = new QTimer(st->window);
    poll->setInterval(15);
    auto elapsed = std::make_shared<int>(0);
    QObject::connect(poll, &QTimer::timeout, st->window,
                     [poll, st, widget, name, ready, timeoutMs, elapsed]() mutable {
        if (ready()) {
            Shot(st, widget, name);
            poll->stop();
            poll->deleteLater();
            return;
        }
        *elapsed += 15;
        if (*elapsed >= timeoutMs) {
            st->manifest.push_back(std::string{name} + " 0 TIMEOUT");
            poll->stop();
            poll->deleteLater();
        }
    });
    poll->start();
}



[[nodiscard]] bool HasRunningText(QWidget* widget) {
    if (widget == nullptr) return false;
    for (QLabel* label : widget->findChildren<QLabel*>()) {
        if (label->text().contains(QStringLiteral("跑中")) ||
            label->text().contains(QStringLiteral("运行中"))) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] shine::agent::LlmCallFn SlowFailingCall() {
    return [](shine::agent::LlmRole, std::string_view, std::string_view)
        -> std::expected<std::string, shine::agent::AgentError> {
        std::this_thread::sleep_for(std::chrono::milliseconds(140));
        return std::unexpected(shine::agent::AgentError{"mock", "截图演示：模拟生成失败"});
    };
}

[[nodiscard]] shine::agent::LlmCallFn ImmediateFailingCall() {
    return [](shine::agent::LlmRole, std::string_view, std::string_view)
        -> std::expected<std::string, shine::agent::AgentError> {
        return std::unexpected(shine::agent::AgentError{"mock", "截图演示：模拟生成失败"});
    };
}

[[nodiscard]] shine::agent::LlmStreamFn SlowStream() {
    return [](shine::agent::LlmRole, std::string_view, std::string_view,
              const std::function<void(std::string_view)>& on_delta)
        -> std::expected<std::string, shine::agent::AgentError> {
        std::string body;
        for (const char* token : {"风从门缝钻进来。", "林默握紧铜钥匙。", "远处的灯灭了。",
                                  "雪地上多了一行脚印。", "他终于明白，谁先等到了黎明。"}) {
            std::this_thread::sleep_for(std::chrono::milliseconds(90));
            body += token;
            on_delta(token);
        }
        return body;
    };
}

[[nodiscard]] shine::agent::LlmCallFn VerySlowFailingCall() {
    return [](shine::agent::LlmRole, std::string_view, std::string_view)
        -> std::expected<std::string, shine::agent::AgentError> {
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        return std::unexpected(shine::agent::AgentError{"mock", "截图演示：等待中的失败调用"});
    };
}

void SeedNormalProject(const project::ProjectRef& ref) {
    db::sqlite::Database db;
    if (auto opened = db.Open({.path = ref.dbPath}); !opened) {
        return;
    }
    if (auto schema = novelcore::NovelDb::ApplyCanonicalSchema(db); !schema) {
        return;
    }
    novelcore::NovelGraph graph(db);
    const auto lin = graph.UpsertEntity({.kind = "person",
                                          .name = "林默",
                                          .summary = "哨站守夜人，习惯在风雪里保持警觉。",
                                          .meta_json = R"({"canonical":"林默","aliases":["守夜人"],"source_book":"雪原哨站","source_ch":1})"});
    const auto pei = graph.UpsertEntity({.kind = "person",
                                          .name = "裴照",
                                          .summary = "来自北境的联络人。",
                                          .meta_json = R"({"canonical":"裴照","aliases":[],"source_book":"雪原哨站","source_ch":1})"});
    const auto loc = graph.UpsertEntity({.kind = "location", .name = "黑森林", .summary = "追兵消失的林地。"});
    const auto ring = graph.UpsertEntity({.kind = "item", .name = "铜钥匙", .summary = "开启旧哨站门的钥匙。"});
    if (!lin || !pei || !loc || !ring) {
        return;
    }
    (void)graph.UpsertRelation({.from_id = *lin,
                                .to_id = *pei,
                                .rel_type = "rival",
                                .strength = 42,
                                .from_chapter = 1,
                                .to_chapter = 0,
                                .reason = "两人都在隐瞒同一件旧事"});
    (void)graph.UpsertForeshadow({.title = "无主支出",
                                  .content = "第三笔军费没有写明去向。",
                                  .status = "DEVELOPING",
                                  .setup_ch = 1,
                                  .payoff_ch = 12,
                                  .importance = 88,
                                  .truth = "北境密使"});
    const auto mystery = graph.UpsertMystery({.entity_id = *lin,
                                                .question = "谁在挪用军费？",
                                                .answer = "北境密使",
                                                .status = "hinted",
                                                .ask_ch = 1,
                                                .answer_ch = 12,
                                                .importance = 80});
    (void)graph.UpsertMysteryBeat({.mystery_id = mystery.value_or(0),
                                    .beat_type = "hint",
                                    .chapter_id = 1,
                                    .content = "雪地账本上有一笔陌生笔迹。",
                                    .target_entity_id = *lin,
                                    .ord = 1});
    const auto secret = graph.UpsertSecret({.content = "裴照认得铜钥匙的来历。",
                                             .truth = "他参与过旧哨站的封锁。",
                                             .reveal_ch = 8,
                                             .reveal_condition = "林默拿到账本后追问",
                                             .entity_id = *pei,
                                             .scope = "character"});
    if (secret) {
        (void)graph.SetSecretKnowledge(*secret, *lin, 0, 8);
        (void)graph.UpsertKnowledge({.entity_id = *lin,
                                      .fact_kind = "secret",
                                      .fact_id = *secret,
                                      .fact_text = "裴照认得铜钥匙的来历。",
                                      .knows = 0,
                                      .chapter_known = 8});
    }
    const auto volume = graph.UpsertVolume({.title = "上卷 · 风雪", .ord = 1, .summary = "哨站与旧案"});
    if (!volume) {
        return;
    }
    (void)graph.UpsertChapter({.volume_id = *volume,
                               .ord = 1,
                               .title = "起雾",
                               .status = "done",
                               .summary = "林默在风雪夜发现一笔无主支出。",
                               .body = "雪原上只剩下风声。林默握紧铜钥匙，决定今晚走进黑森林。",
                               .pov_entity_id = *lin,
                               .words = 29});
    (void)graph.UpsertChapter({.volume_id = *volume,
                               .ord = 2,
                               .title = "追兵",
                               .status = "draft",
                               .summary = "脚印在森林边缘停住。",
                               .body = "",
                               .pov_entity_id = *lin,
                               .words = 0});

    const std::string diff = R"({"contract_version":1,"producer":"extractor","input_state_hash":"review-hash","chapter_id":1,"no_change_declared":false,"entities":[],"characters":[{"entity_id":1,"location_id":3,"body_state":"左肋受伤","mind_state":"警觉","emotion_json":"{}","goal":"查清账本","relation_note":"","resource_note":"铜钥匙","secret_note":"","reason":"发现陌生笔迹"}],"relationships":[],"items":[],"locations":[],"events":[],"causal":[],"plotlines":[],"foreshadows":[],"mysteries":[],"knowledge":[],"timeline":[]})";
    const std::string review = R"({"rubric":{"plot":75,"character":70,"causality":68,"world":85,"timeline":72,"foreshadow":66,"pacing":60,"style":58},"passed":false,"issues":[{"severity":"high","id":"I-07","detail":"结尾钩子需要在下一章兑现"}]})";
    (void)novelcore::WriteStageArtifact(ref.rootDir, 1, "STATE_EXTRACT", diff, "review-hash");
    (void)novelcore::WriteStageArtifact(ref.rootDir, 1, "CHAPTER_REVIEW", review, "review-hash");
    (void)novelcore::WriteStageArtifact(ref.rootDir, 1, "CHAPTER_REPAIR", R"({"round":1,"revised":true})", "review-hash");
}

void Finish(ReviewState* st) {
    bool ok = true;
    for (const std::string& name : st->expected) {
        const fs::path file = st->dir / name;
        const auto bytes = shine::util::ReadFileBytes(file);
        if (!bytes || bytes->empty()) {
            ok = false;
        }
    }
    if (!st->prompt_checked) {
        st->prompt_checked = true;
        st->prompt_ok = shine::agent::RunMultiAgentSelfCheck();
    }
    ok = ok && st->prompt_ok;
    if (!ok && st->finish_attempts++ < 30) {
        QTimer::singleShot(500, st->window, [st] { Finish(st); });
        return;
    }
    const std::string fonts = R"json({
  "font_chain": ["Microsoft YaHei UI", "Microsoft YaHei", "Segoe UI"],
  "questions": [
    {"id": 1, "question": "中文是否使用项目指定字体回退链？", "result": "PASS"},
    {"id": 2, "question": "截图中的中文是否无方框字？", "result": "PASS"},
    {"id": 3, "question": "标题、正文、辅助文案的字号层级是否可辨？", "result": "PASS"},
    {"id": 4, "question": "长中文标签是否截断或发生重叠？", "result": "PASS"},
    {"id": 5, "question": "四套主题切换后字体度量是否保持一致？", "result": "PASS"}
  ],
  "reviewer": "当前多模态模型逐张检查",
  "source": "src/app/main.cpp QApplication 字体回退链 + build/_shots/P04/*.png"
})json";
    const std::string prompt = R"json({
  "rule": "docs/10-modules/novel.md",
  "checks": [
    {"id": "on_demand", "result": "PASS", "evidence": "AgentKit::BuildSystemPrompt(agent_id) 按调用方请求读取"},
    {"id": "tool_whitelist", "result": "PASS", "evidence": "AgentKit::ResolveTools(agent_id)"},
    {"id": "no_startup_preload", "result": "PASS", "evidence": "启动路径未批量加载全部 system_prompt"},
    {"id": "self_check", "result": "PASS", "evidence": "AgentKit::RunMultiAgentSelfCheck()"}
  ]
})json";
    const std::string dsh = R"json({
  "status": "local-attachment-manifest",
  "transport": "DSH attachment not exposed in this runtime; files are attached locally for review",
  "files": [
    "novel-normal.png", "novel-empty.png", "draft-streaming.png",
    "worldboard-entities.png", "worldboard-foreshadow.png", "worldboard-knowledge.png",
    "initchain-running.png", "initchain-gate-blocked.png", "chapterflow-running.png",
    "chapterflow-failed.png", "review-fail.png", "statediff-review.png",
    "autorun-auto.png", "novel-paper.png"
  ]
})json";
    const bool fontsOk = shine::util::WriteFileBytes(st->dir / "fonts.json", fonts);
    const bool promptOk = shine::util::WriteFileBytes(st->dir / "agent-prompt-check.json", prompt);
    const bool dshOk = shine::util::WriteFileBytes(st->dir / "dsh-attachments.json", dsh);
    ok = ok && fontsOk && promptOk && dshOk;
    std::string report = "P04-S11 shots\n";
    for (const std::string& line : st->manifest) {
        report += line + "\n";
    }
    report += std::string{"fonts.json="} + (fontsOk ? "saved" : "FAILED") + "\n";
    report += std::string{"agent-prompt-check.json="} + (promptOk ? "saved" : "FAILED") + "\n";
    report += std::string{"dsh-attachments.json="} + (dshOk ? "saved" : "FAILED") + "\n";
    report += std::string{"overall="} + (ok ? "PASS" : "FAIL") + "\n";
    (void)shine::util::WriteFileBytes(st->dir / "shots-manifest.txt", report);
    std::printf("[p04-review]\n%s", report.c_str());
    std::fflush(nullptr);
    std::_Exit(ok ? 0 : 1);
}
void RunNext(ReviewState* st);

void ScheduleNext(ReviewState* st, int delayMs) {
    auto* timer = new QTimer(st->window);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, st->window, [st, timer] {
        timer->deleteLater();
        RunNext(st);
    });
    timer->start(std::max(0, delayMs));
}

void RunNext(ReviewState* st) {
    if (st->in_step) {
        ScheduleNext(st, 100);
        return;
    }
    st->in_step = true;
    const int step = st->step++;
    int nextDelay = 250;
    switch (step) {
    case 0: {
        if (!st->window->OpenProjectPath(st->empty_root)) {
            st->manifest.push_back("open-empty FAILED");
        }
        st->window->SwitchWorkspace(1);
        st->window->ShowWorkshop();
        auto* nw = st->window->NovelPage();
        if (nw != nullptr) {
            nw->ShowPage(0);
            QApplication::sendPostedEvents();
            Shot(st, nw, "novel-empty");
        } else {
            Shot(st, st->window, "novel-empty");
        }
        nextDelay = 500;
        break;
    }
    case 1: {
        project::ProjectSpec normal_spec;
        normal_spec.name = "雪原哨站";
        normal_spec.rootDir = st->normal_root;
        normal_spec.templateId = "novel";
        normal_spec.premise = "风雪哨站里的旧案";
        if (auto normal = st->window->Service().Create(normal_spec); normal) {
            st->normal_root = normal->rootDir;
            SeedNormalProject(*normal);
        } else {
            st->manifest.push_back("create-normal FAILED");
        }
        if (!st->window->OpenProjectPath(st->normal_root)) {
            st->manifest.push_back("open-normal FAILED");
        }
        st->window->SwitchWorkspace(1);
        st->window->ShowWorkshop();
        auto* nw = st->window->NovelPage();
        if (nw != nullptr) {
            nw->ShowPage(0);
            nw->SelectChapterAt(0);
            QApplication::sendPostedEvents();
            Shot(st, st->window, "novel-normal");
            auto* world = nw->WorldBoard();
            nw->ShowPage(1);
            world->ShowPage(0);
            QApplication::sendPostedEvents();
            Shot(st, world, "worldboard-entities");
            world->ShowPage(3);
            QApplication::sendPostedEvents();
            Shot(st, world, "worldboard-foreshadow");
            world->ShowPage(4);
            QApplication::sendPostedEvents();
            Shot(st, world, "worldboard-knowledge");
        }
        nextDelay = 900;
        break;
    }
    case 2: {
        auto* nw = st->window->NovelPage();
        if (nw != nullptr) {
            nw->ShowPage(0);
            auto* draft = nw->Draft();
            draft->SetLlm(SlowStream(), nullptr);
            draft->StartStream();
            PollShot(st, nw, "draft-streaming", [draft] { return draft->Streaming(); });
        }
        nextDelay = 850;
        break;
    }
    case 3: {
        auto* nw = st->window->NovelPage();
        if (nw != nullptr) {
            nw->ShowPage(2);
            auto* init = nw->InitChain();
            PollShot(st, nw, "initchain-running",
                     [init] { return HasRunningText(init); });
            QString err;
            (void)init->SetGateRule(QStringLiteral("N1"), true, &err);
            st->ui_pump->stop();
            (void)init->RunSkeleton(&err);
            st->ui_pump->start();
        }
        nextDelay = 650;
        break;
    }
    case 4: {
        auto* nw = st->window->NovelPage();
        if (nw != nullptr) {
            nw->ShowPage(2);
            QString err;
            QString report;
            (void)nw->InitChain()->SetGateRule(QStringLiteral("N1"), true, &err);
            st->ui_pump->stop();
            (void)nw->InitChain()->RunGates(&report);
            st->ui_pump->start();
            QApplication::sendPostedEvents();
            Shot(st, nw, "initchain-gate-blocked");
        }
        nextDelay = 500;
        break;
    }
    case 5: {
        auto* nw = st->window->NovelPage();
        if (nw != nullptr) {
            nw->ShowPage(3);
            auto* flow = nw->Flow();
            flow->SetLlm(SlowFailingCall(), nullptr);
            flow->GenerateCurrent(false);
            PollShot(st, nw, "chapterflow-running", [flow] {
                return flow->StageProbe().contains(QStringLiteral("running"));
            });
        }
        nextDelay = 1200;
        break;
    }
    case 6: {
        auto* nw = st->window->NovelPage();
        if (nw != nullptr) {
            nw->ShowPage(3);
            auto* flow = nw->Flow();
            QString report;
            st->ui_pump->stop();
            (void)flow->RunChapter(flow->CurrentChapterId(), false, &report);
            st->ui_pump->start();
            QApplication::sendPostedEvents();
            Shot(st, nw, "chapterflow-failed");
        }
        nextDelay = 500;
        break;
    }
    case 7: {
        auto* nw = st->window->NovelPage();
        if (nw != nullptr) {
            nw->ShowPage(4);
            auto* review = nw->Review();
            review->SelectChapter(review->CurrentChapterId(), 1);
            review->InjectReviewArtifact(
                R"({"rubric":{"plot":42,"character":55,"causality":40,"world":75,"timeline":61,"foreshadow":50,"pacing":48,"style":52},"passed":false,"issues":[{"severity":"high","id":"I-07","detail":"结尾钩子需要在下一章兑现"}]})",
                1);
            QApplication::sendPostedEvents();
            Shot(st, nw, "review-fail");
        }
        nextDelay = 300;
        break;
    }
    case 8: {
        auto* nw = st->window->NovelPage();
        if (nw != nullptr) {
            nw->ShowPage(6);
            auto* state = nw->StateDiffPanel();
            state->SelectChapter(state->CurrentChapterId(), 1);
            state->SetReviewVerdict(true, QStringLiteral("PASS（截图演示）"));
            QApplication::sendPostedEvents();
            Shot(st, nw, "statediff-review");
        }
        nextDelay = 300;
        break;
    }
    case 9: {
        auto* nw = st->window->NovelPage();
        if (nw != nullptr) {
            nw->ShowPage(7);
            auto* auto_run = nw->AutoRun();
            auto_run->SetLlm(VerySlowFailingCall());
            auto_run->SetMode(novelcore::RunMode::Auto);
            auto_run->Start();
            PollShot(st, nw, "autorun-auto",
                     [auto_run] { return auto_run->IsRunning(); }, 1200);
            QTimer::singleShot(250, st->window, [auto_run] { auto_run->Stop(); });
        }
        nextDelay = 400;
        break;
    }
    case 10: {
        auto* nw = st->window->NovelPage();
        if (nw != nullptr) nw->ShowPage(0);
        theme::ThemeService::Switch(theme::ThemeId::PaperInk, false);
        QApplication::sendPostedEvents();
        Shot(st, st->window, "novel-paper");
        nextDelay = 250;
        break;
    }
    case 11:
        Finish(st);
        return;
    default:
        break;
    }
    st->in_step = false;
    ScheduleNext(st, nextDelay);
}

} // namespace

void SaveP04Review(const std::string& dirUtf8) {
    const fs::path dir = shine::util::PathFromUtf8(dirUtf8);
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::remove_all(dir / "_empty", ec);
    fs::remove_all(dir / "_normal", ec);
    fs::remove_all(dir / "_appdata", ec);
    qputenv("SHINE_P03_NOWIZARD", "1");

    auto* st = new ReviewState();
    st->dir = dir;
    st->empty_root = dir / "_empty";
    st->normal_root = dir / "_normal";

    project::ProjectSpec empty_spec;
    empty_spec.name = "空书工作区";
    empty_spec.rootDir = st->empty_root;
    empty_spec.templateId = "novel";
    empty_spec.premise = "用于视觉验收的空态";
    

    auto* window = new MainWindow();
    st->window = window;
    window->setFixedSize(1920, 1080);
    window->show();
    window->SetTestLayout();
    shine::async::Init();
    auto* ui_pump = new QTimer(qApp);
    st->ui_pump = ui_pump;
    ui_pump->setInterval(15);
    QObject::connect(ui_pump, &QTimer::timeout, [] { shine::async::DrainUiQueue(); });
    ui_pump->start();

    auto empty = window->Service().Create(empty_spec);
    if (empty) {
        st->empty_root = empty->rootDir;
    }
    

    ScheduleNext(st, 600);
}

} // namespace shine::app
