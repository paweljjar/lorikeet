#include <functional>
#include <map>
#include <string>
#include <vector>

#include "Database.h"
#include "Json.h"
#include "Server.h"
#include "UIEngine.h"

using namespace HTTPServer;

namespace {
    class TopBar : public UIEngine::Component { // Friendly gamified header bar with Lorikeet mascot, navigation tabs, and word counter pill
        public:
            using Component::Component;

            [[nodiscard]] std::string getComponentName() const override { return "TopBar"; }

            [[nodiscard]] UIEngine::VNode render() const override {
                const std::string count = getProp("count", "0");
                return UIEngine::Tags::header({{"class", "topbar"}}, {
                    UIEngine::Tags::div({{"class", "brand"}}, {
                        UIEngine::Tags::span({{"class", "brand-icon"}}, "🦜"),
                        UIEngine::Tags::span({{"class", "brand-name"}}, getProp("title", "Lorikeet"))
                    }),
                    UIEngine::Tags::nav({{"class", "topbar-nav"}}, {
                        UIEngine::Link("/deck", "📚 Deck", {{"defaultActive", "true"}}),
                        UIEngine::Link("/practice", "🎯 Practice"),
                        UIEngine::Link("/stats", "📊 Stats")
                    }),
                    UIEngine::Tags::div({{"class", "counter-pill"}}, {
                        UIEngine::VNode::textNode("⭐ " + count + " Words")
                    })
                });
            }

            [[nodiscard]] UIEngine::StyleSheet styles() const override {
                UIEngine::StyleSheet s;
                s.rule(".topbar", {
                    {"display", "flex"},
                    {"justify-content", "space-between"},
                    {"align-items", "center"},
                    {"flex-wrap", "wrap"},
                    {"gap", "1rem"},
                    {"padding-bottom", "1.25rem"},
                    {"margin-bottom", "2rem"},
                    {"border-bottom", "2px solid #e5e5e5"}
                })
                .rule(".brand", {
                    {"display", "flex"},
                    {"align-items", "center"},
                    {"gap", "0.6rem"}
                })
                .rule(".brand-icon", {
                    {"font-size", "1.75rem"}
                })
                .rule(".brand-name", {
                    {"font-size", "1.7rem"},
                    {"font-weight", "800"},
                    {"color", "#1cb0f6"},
                    {"letter-spacing", "-0.5px"}
                })
                .rule(".topbar-nav", {
                    {"display", "flex"},
                    {"align-items", "center"},
                    {"gap", "0.5rem"}
                })
                .rule(".nav-link", {
                    {"text-decoration", "none"},
                    {"color", "#777777"},
                    {"font-weight", "800"},
                    {"font-size", "0.95rem"},
                    {"padding", "0.5rem 0.95rem"},
                    {"border-radius", "0.85rem"},
                    {"border", "2px solid transparent"},
                    {"transition", "background 0.12s ease, color 0.12s ease, border-color 0.12s ease"}
                })
                .rule(".nav-link:hover", {
                    {"background", "#f0f0f0"},
                    {"color", "#3c3c3c"}
                })
                .rule(".nav-link.active", {
                    {"background", "#ddf4ff"},
                    {"color", "#1cb0f6"},
                    {"border-color", "#84d8ff"}
                })
                .rule(".counter-pill", {
                    {"background", "#fff4e5"},
                    {"color", "#ff9600"},
                    {"border", "2px solid #ffd8a8"},
                    {"border-radius", "999px"},
                    {"padding", "0.45rem 1rem"},
                    {"font-weight", "800"},
                    {"font-size", "0.95rem"}
                });
                return s;
            }
    };

    class Counter : public UIEngine::Component { // Reusable stateful counter demonstrating scoped multi-instance isolation
        public:
            using Component::Component;

            [[nodiscard]] std::string getComponentName() const override { return "Counter"; }

            void onMount() override {
                const int initial = std::stoi(getProp("initial", "0"));
                useState<int>("count", initial).set(initial);
            }

            [[nodiscard]] UIEngine::VNode render() const override {
                const std::string label = getProp("label", "Counter");
                UIEngine::State<int> count = useState<int>("count", 0);

                return UIEngine::Tags::div({{"class", "counter-card"}}, {
                    UIEngine::Tags::span({{"class", "counter-label"}}, label),
                    UIEngine::Tags::div({{"class", "counter-controls"}}, {
                        UIEngine::Tags::button({
                            {"class", "btn-counter"},
                            {"onClick", [count] { count.set(count.get() - 1); }}
                        }, "-"),
                        UIEngine::Tags::span({{"class", "counter-value"}}, std::to_string(count.get())),
                        UIEngine::Tags::button({
                            {"class", "btn-counter"},
                            {"onClick", [count] { count.set(count.get() + 1); }}
                        }, "+")
                    })
                });
            }

            [[nodiscard]] UIEngine::StyleSheet styles() const override {
                UIEngine::StyleSheet s;
                s.rule(".counter-card", {
                    {"background", "#ffffff"},
                    {"border", "2px solid #e5e5e5"},
                    {"border-bottom", "4px solid #e5e5e5"},
                    {"border-radius", "1rem"},
                    {"padding", "0.85rem 1.15rem"},
                    {"display", "flex"},
                    {"justify-content", "space-between"},
                    {"align-items", "center"},
                    {"flex", "1 1 200px"}
                })
                .rule(".counter-label", {
                    {"font-weight", "800"},
                    {"font-size", "0.95rem"},
                    {"color", "#4b4b4b"}
                })
                .rule(".counter-controls", {
                    {"display", "flex"},
                    {"align-items", "center"},
                    {"gap", "0.75rem"}
                })
                .rule(".counter-value", {
                    {"font-size", "1.2rem"},
                    {"font-weight", "800"},
                    {"min-width", "2rem"},
                    {"text-align", "center"},
                    {"color", "#1cb0f6"}
                })
                .rule(".btn-counter", {
                    {"background", "#f0f0f0"},
                    {"border", "2px solid #e0e0e0"},
                    {"border-radius", "0.6rem"},
                    {"width", "2.1rem"},
                    {"height", "2.1rem"},
                    {"font-size", "1.1rem"},
                    {"font-weight", "800"},
                    {"cursor", "pointer"},
                    {"color", "#4b4b4b"},
                    {"display", "flex"},
                    {"align-items", "center"},
                    {"justify-content", "center"},
                    {"transition", "background 0.1s"}
                })
                .rule(".btn-counter:hover", {
                    {"background", "#e2e2e2"}
                });
                return s;
            }
    };

    class AddWordCard : public UIEngine::Component { // Tactile card with form to add a new word and translation via C++ onSubmit handler
        public:
            using Component::Component;

            [[nodiscard]] std::string getComponentName() const override { return "AddWordCard"; }

            [[nodiscard]] UIEngine::VNode render() const override {
                const UIEngine::EventHandler onAdded = getCallback("onAdded");
                return UIEngine::Tags::section({{"class", "add-card"}}, {
                    UIEngine::Tags::h2({{"class", "section-title"}}, "Add a New Word"),
                    UIEngine::Tags::p({{"class", "section-subtitle"}}, "Type a word and its translation to grow your vocabulary collection."),
                    UIEngine::Tags::form({
                        {"class", "word-form"},
                        {"onSubmit", [onAdded](const UIEngine::Event& e) {
                            const auto wordIt = e.form.find("word");
                            const auto transIt = e.form.find("translation");
                            if (wordIt == e.form.end() || transIt == e.form.end()) {
                                return;
                            }
                            const UIEngine::FetchResponse res = UIEngine::fetch("/words", {
                                .method = "POST",
                                .body = Json::Object{
                                    {"word", wordIt->second},
                                    {"translation", transIt->second}
                                }
                            });
                            if (res.ok() && onAdded) {
                                onAdded(e);
                            }
                        }}
                    }, {
                        UIEngine::Tags::input({
                            {"name", "word"},
                            {"placeholder", "Word (e.g. Bonjour)"},
                            {"required", "true"}
                        }),
                        UIEngine::Tags::input({
                            {"name", "translation"},
                            {"placeholder", "Translation (e.g. Good morning)"},
                            {"required", "true"}
                        }),
                        UIEngine::Tags::button({{"type", "submit"}, {"class", "btn-add"}}, "ADD WORD")
                    })
                });
            }

            [[nodiscard]] UIEngine::StyleSheet styles() const override {
                UIEngine::StyleSheet s;
                s.rule(".add-card", {
                    {"background", "#ffffff"},
                    {"border", "2px solid #e5e5e5"},
                    {"border-bottom", "5px solid #e5e5e5"},
                    {"border-radius", "1.25rem"},
                    {"padding", "1.5rem"},
                    {"margin-bottom", "2.25rem"}
                })
                .rule(".section-title", {
                    {"margin", "0 0 0.25rem 0"},
                    {"font-size", "1.3rem"},
                    {"font-weight", "800"},
                    {"color", "#3c3c3c"}
                })
                .rule(".section-subtitle", {
                    {"margin", "0 0 1.25rem 0"},
                    {"color", "#777777"},
                    {"font-size", "0.95rem"}
                })
                .rule(".word-form", {
                    {"display", "flex"},
                    {"flex-wrap", "wrap"},
                    {"gap", "0.75rem"}
                })
                .rule(".word-form input", {
                    {"flex", "1 1 200px"},
                    {"background", "#f7f7f7"},
                    {"color", "#3c3c3c"},
                    {"border", "2px solid #e5e5e5"},
                    {"border-bottom", "4px solid #e5e5e5"},
                    {"border-radius", "0.9rem"},
                    {"padding", "0.8rem 1rem"},
                    {"font-size", "1rem"},
                    {"font-weight", "600"},
                    {"outline", "none"}
                })
                .rule(".word-form input:focus", {
                    {"background", "#ffffff"},
                    {"border-color", "#1cb0f6"}
                })
                .rule(".btn-add", {
                    {"background", "#58cc02"},
                    {"color", "#ffffff"},
                    {"border", "none"},
                    {"border-bottom", "4px solid #58a700"},
                    {"border-radius", "0.9rem"},
                    {"padding", "0.8rem 1.5rem"},
                    {"font-weight", "800"},
                    {"font-size", "0.95rem"},
                    {"letter-spacing", "0.5px"},
                    {"cursor", "pointer"}
                })
                .rule(".btn-add:hover", {
                    {"filter", "brightness(1.05)"}
                })
                .rule(".btn-add:active", {
                    {"transform", "translateY(2px)"},
                    {"border-bottom-width", "2px"}
                });
                return s;
            }
    };

    class WordCard : public UIEngine::Component { // Individual vocabulary flashcard component with C++ onClick delete handler
        public:
            using Component::Component;

            [[nodiscard]] std::string getComponentName() const override { return "WordCard"; }

            [[nodiscard]] UIEngine::VNode render() const override {
                const std::string id = getProp("id", "0");
                const UIEngine::EventHandler onDeleted = getCallback("onDeleted");
                return UIEngine::Tags::div({{"class", "word-card"}}, {
                    UIEngine::Tags::div({{"class", "word-card-top"}}, {
                        UIEngine::Tags::span({{"class", "word-tag"}}, "CARD #" + id),
                        UIEngine::Tags::button({
                            {"class", "btn-delete"},
                            {"title", "Delete card"},
                            {"onClick", [id, onDeleted](const UIEngine::Event& e) {
                                const UIEngine::FetchResponse res = UIEngine::fetch("/words/delete?id=" + id, {
                                    .method = "POST"
                                });
                                if (res.ok() && onDeleted) {
                                    onDeleted(e);
                                }
                            }}
                        }, "✕")
                    }),
                    UIEngine::Tags::h3({{"class", "word-term"}}, getProp("word", "")),
                    UIEngine::Tags::p({{"class", "word-translation"}}, getProp("translation", ""))
                });
            }

            [[nodiscard]] UIEngine::StyleSheet styles() const override {
                UIEngine::StyleSheet s;
                s.rule(".word-card", {
                    {"background", "#ffffff"},
                    {"border", "2px solid #e5e5e5"},
                    {"border-bottom", "5px solid #e5e5e5"},
                    {"border-radius", "1.15rem"},
                    {"padding", "1.15rem 1.25rem"},
                    {"transition", "transform 0.12s ease, border-color 0.12s ease"}
                })
                .rule(".word-card:hover", {
                    {"border-color", "#1cb0f6"},
                    {"transform", "translateY(-2px)"}
                })
                .rule(".word-card-top", {
                    {"display", "flex"},
                    {"justify-content", "space-between"},
                    {"align-items", "center"},
                    {"margin-bottom", "0.5rem"}
                })
                .rule(".word-tag", {
                    {"background", "#ddf4ff"},
                    {"color", "#1cb0f6"},
                    {"font-size", "0.72rem"},
                    {"font-weight", "800"},
                    {"padding", "0.2rem 0.55rem"},
                    {"border-radius", "0.5rem"}
                })
                .rule(".btn-delete", {
                    {"background", "transparent"},
                    {"color", "#afafaf"},
                    {"border", "none"},
                    {"font-size", "0.95rem"},
                    {"font-weight", "800"},
                    {"cursor", "pointer"},
                    {"padding", "0.15rem 0.4rem"},
                    {"border-radius", "0.4rem"}
                })
                .rule(".btn-delete:hover", {
                    {"background", "#ffdfe0"},
                    {"color", "#ff4b4b"}
                })
                .rule(".word-term", {
                    {"margin", "0 0 0.3rem 0"},
                    {"font-size", "1.3rem"},
                    {"font-weight", "800"},
                    {"color", "#3c3c3c"}
                })
                .rule(".word-translation", {
                    {"margin", "0"},
                    {"font-size", "1rem"},
                    {"font-weight", "600"},
                    {"color", "#777777"}
                });
                return s;
            }
    };

    class PracticeView : public UIEngine::Component { // Interactive flashcard flip & review view mounted at /practice
        public:
            using Component::Component;

            [[nodiscard]] std::string getComponentName() const override { return "PracticeView"; }

            [[nodiscard]] UIEngine::VNode render() const override {
                const int total = std::stoi(getProp("total", "0"));
                if (total <= 0) {
                    return UIEngine::Tags::section({{"class", "practice-empty"}}, {
                        UIEngine::Tags::h2({{"class", "section-title"}}, "No Cards to Practice Yet"),
                        UIEngine::Tags::p({{"class", "section-subtitle"}}, "Add some words to your deck first to start a practice session."),
                        UIEngine::Tags::button({
                            {"class", "btn-add"},
                            {"onClick", [] { UIEngine::navigate("/deck"); }}
                        }, "GO TO DECK")
                    });
                }

                UIEngine::State<int> indexState = useState<int>("index", 0);
                UIEngine::State<bool> flippedState = useState<bool>("flipped", false);

                int idx = indexState.get() % total;
                if (idx < 0) idx += total;

                const std::string word = getProp("word_" + std::to_string(idx), "");
                const std::string translation = getProp("trans_" + std::to_string(idx), "");
                const bool isFlipped = flippedState.get();

                return UIEngine::Tags::section({{"class", "practice-container"}}, {
                    UIEngine::Tags::div({{"class", "practice-header"}}, {
                        UIEngine::Tags::h2({{"class", "section-title"}}, "Flashcard Practice"),
                        UIEngine::Tags::span({{"class", "practice-progress"}},
                            "Card " + std::to_string(idx + 1) + " of " + std::to_string(total))
                    }),
                    UIEngine::Tags::div({
                        {"class", isFlipped ? "flashcard flipped" : "flashcard"},
                        {"onClick", [flippedState] { flippedState.set(!flippedState.get()); }}
                    }, {
                        UIEngine::Tags::span({{"class", "flashcard-badge"}},
                            isFlipped ? "TRANSLATION" : "WORD"),
                        UIEngine::Tags::h3({{"class", "flashcard-main"}},
                            isFlipped ? translation : word),
                        UIEngine::Tags::p({{"class", "flashcard-hint"}},
                            isFlipped ? "Click card to flip back to word" : "Click card to reveal translation")
                    }),
                    UIEngine::Tags::div({{"class", "practice-controls"}}, {
                        UIEngine::Tags::button({
                            {"class", "btn-secondary"},
                            {"onClick", [indexState, flippedState, idx, total] {
                                indexState.set((idx - 1 + total) % total);
                                flippedState.set(false);
                            }}
                        }, "← PREVIOUS"),
                        UIEngine::Tags::button({
                            {"class", "btn-flip"},
                            {"onClick", [flippedState] {
                                flippedState.set(!flippedState.get());
                            }}
                        }, isFlipped ? "HIDE ANSWER" : "SHOW ANSWER"),
                        UIEngine::Tags::button({
                            {"class", "btn-secondary"},
                            {"onClick", [indexState, flippedState, idx, total] {
                                indexState.set((idx + 1) % total);
                                flippedState.set(false);
                            }}
                        }, "NEXT →")
                    })
                });
            }

            [[nodiscard]] UIEngine::StyleSheet styles() const override {
                UIEngine::StyleSheet s;
                s.rule(".practice-container", {
                    {"display", "flex"},
                    {"flex-direction", "column"},
                    {"gap", "1.25rem"}
                })
                .rule(".practice-header", {
                    {"display", "flex"},
                    {"justify-content", "space-between"},
                    {"align-items", "center"}
                })
                .rule(".practice-progress", {
                    {"font-weight", "800"},
                    {"color", "#1cb0f6"},
                    {"background", "#ddf4ff"},
                    {"padding", "0.35rem 0.85rem"},
                    {"border-radius", "999px"},
                    {"font-size", "0.9rem"}
                })
                .rule(".flashcard", {
                    {"background", "#ffffff"},
                    {"border", "2px solid #e5e5e5"},
                    {"border-bottom", "6px solid #e5e5e5"},
                    {"border-radius", "1.5rem"},
                    {"padding", "3.25rem 2rem"},
                    {"text-align", "center"},
                    {"cursor", "pointer"},
                    {"user-select", "none"},
                    {"transition", "transform 0.12s ease, border-color 0.12s ease, background 0.12s ease"}
                })
                .rule(".flashcard:hover", {
                    {"border-color", "#1cb0f6"},
                    {"transform", "translateY(-2px)"}
                })
                .rule(".flashcard.flipped", {
                    {"background", "#f0fbe8"},
                    {"border-color", "#58cc02"}
                })
                .rule(".flashcard-badge", {
                    {"display", "inline-block"},
                    {"font-size", "0.75rem"},
                    {"font-weight", "800"},
                    {"letter-spacing", "0.8px"},
                    {"color", "#777777"},
                    {"background", "#f0f0f0"},
                    {"padding", "0.25rem 0.7rem"},
                    {"border-radius", "0.5rem"},
                    {"margin-bottom", "1rem"}
                })
                .rule(".flashcard-main", {
                    {"margin", "0 0 1rem 0"},
                    {"font-size", "2.2rem"},
                    {"font-weight", "800"},
                    {"color", "#3c3c3c"}
                })
                .rule(".flashcard-hint", {
                    {"margin", "0"},
                    {"font-size", "0.9rem"},
                    {"font-weight", "600"},
                    {"color", "#999999"}
                })
                .rule(".practice-controls", {
                    {"display", "flex"},
                    {"justify-content", "space-between"},
                    {"gap", "0.75rem"},
                    {"flex-wrap", "wrap"}
                })
                .rule(".btn-secondary", {
                    {"background", "#ffffff"},
                    {"color", "#4b4b4b"},
                    {"border", "2px solid #e5e5e5"},
                    {"border-bottom", "4px solid #e5e5e5"},
                    {"border-radius", "0.9rem"},
                    {"padding", "0.8rem 1.25rem"},
                    {"font-weight", "800"},
                    {"font-size", "0.9rem"},
                    {"cursor", "pointer"}
                })
                .rule(".btn-secondary:hover", {
                    {"background", "#f7f7f7"}
                })
                .rule(".btn-flip", {
                    {"background", "#1cb0f6"},
                    {"color", "#ffffff"},
                    {"border", "none"},
                    {"border-bottom", "4px solid #1899d6"},
                    {"border-radius", "0.9rem"},
                    {"padding", "0.8rem 1.6rem"},
                    {"font-weight", "800"},
                    {"font-size", "0.9rem"},
                    {"cursor", "pointer"}
                })
                .rule(".btn-flip:hover", {
                    {"filter", "brightness(1.05)"}
                })
                .rule(".practice-empty", {
                    {"background", "#ffffff"},
                    {"border", "2px dashed #e5e5e5"},
                    {"border-radius", "1.25rem"},
                    {"padding", "2.75rem 1.5rem"},
                    {"text-align", "center"}
                });
                return s;
            }
    };

    class App : public UIEngine::Component { // Stateful C++ frontend application component managing vocabulary flashcards across multiple routes
        public:
            using Component::Component;

            [[nodiscard]] std::string getComponentName() const override { return "App"; }

            void onMount() override {
                refreshWords();
            }

            void refreshWords() const {
                const UIEngine::FetchResponse res = UIEngine::fetch("/words");
                if (res.ok() && res.json().isArray()) {
                    _words = res.json().asArray();
                }
            }

            [[nodiscard]] UIEngine::VNode renderDeckView(const Json::Array& wordList) const {
                std::vector<UIEngine::VNode> cardNodes;

                if (wordList.empty()) {
                    cardNodes.push_back(
                        UIEngine::Tags::div({{"class", "empty-state"}}, {
                            UIEngine::Tags::p({{"class", "empty-title"}}, "No flashcards yet!"),
                            UIEngine::Tags::p({{"class", "empty-sub"}}, "Add your first word and translation above.")
                        })
                    );
                } else {
                    cardNodes.reserve(wordList.size());
                    for (const Json& item : wordList) {
                        if (!item.isObject()) continue;
                        const Json::Object& obj = item.asObject();
                        const std::string id = obj.contains("id")
                            ? std::to_string(static_cast<long long>(obj.at("id").asNumber()))
                            : "0";
                        const std::string word = obj.contains("word") && obj.at("word").isString()
                            ? obj.at("word").asString()
                            : "";
                        const std::string translation = obj.contains("translation") && obj.at("translation").isString()
                            ? obj.at("translation").asString()
                            : "";

                        cardNodes.push_back(WordCard({
                            {"key", id},
                            {"id", id},
                            {"word", word},
                            {"translation", translation},
                            {"onDeleted", [this] { refreshWords(); }}
                        }));
                    }
                }

                return UIEngine::Tags::div({}, {
                    AddWordCard({
                        {"onAdded", [this] { refreshWords(); }}
                    }),
                    UIEngine::Tags::section({}, {
                        UIEngine::Tags::h2({{"class", "deck-heading"}}, "Saved Flashcards"),
                        UIEngine::Tags::div({{"class", "cards-grid"}}, std::move(cardNodes))
                    })
                });
            }

            [[nodiscard]] UIEngine::VNode renderPracticeView(const Json::Array& wordList) const {
                UIEngine::Props practiceProps;
                int count = 0;
                for (const Json& item : wordList) {
                    if (!item.isObject()) continue;
                    const Json::Object& obj = item.asObject();
                    const std::string word = obj.contains("word") && obj.at("word").isString()
                        ? obj.at("word").asString()
                        : "";
                    const std::string translation = obj.contains("translation") && obj.at("translation").isString()
                        ? obj.at("translation").asString()
                        : "";
                    practiceProps["word_" + std::to_string(count)] = word;
                    practiceProps["trans_" + std::to_string(count)] = translation;
                    ++count;
                }
                practiceProps["total"] = std::to_string(count);
                return PracticeView(std::move(practiceProps));
            }

            [[nodiscard]] UIEngine::VNode renderStatsView(const Json::Array& wordList) const {
                std::size_t totalChars = 0;
                std::string longestWord = "—";
                std::size_t maxLen = 0;

                for (const Json& item : wordList) {
                    if (!item.isObject()) continue;
                    const Json::Object& obj = item.asObject();
                    if (obj.contains("word") && obj.at("word").isString()) {
                        const std::string& w = obj.at("word").asString();
                        totalChars += w.size();
                        if (w.size() > maxLen) {
                            maxLen = w.size();
                            longestWord = w;
                        }
                    }
                }

                const std::string avgLength = wordList.empty()
                    ? "0"
                    : std::to_string(totalChars / wordList.size()) + " chars";

                return UIEngine::Tags::div({{"class", "stats-view"}}, {
                    UIEngine::Tags::h2({{"class", "deck-heading"}}, "Learning Goals"),
                    UIEngine::Tags::section({{"class", "goals-section"}}, {
                        Counter({{"label", "🎯 Daily Goal"}, {"initial", "10"}}),
                        Counter({{"label", "🔥 Streak Count"}, {"initial", "3"}})
                    }),
                    UIEngine::Tags::h2({{"class", "deck-heading"}}, "Deck Statistics"),
                    UIEngine::Tags::div({{"class", "stats-grid"}}, {
                        UIEngine::Tags::div({{"class", "stat-box"}}, {
                            UIEngine::Tags::span({{"class", "stat-label"}}, "TOTAL CARDS"),
                            UIEngine::Tags::span({{"class", "stat-value"}}, std::to_string(wordList.size()))
                        }),
                        UIEngine::Tags::div({{"class", "stat-box"}}, {
                            UIEngine::Tags::span({{"class", "stat-label"}}, "AVG WORD LENGTH"),
                            UIEngine::Tags::span({{"class", "stat-value"}}, avgLength)
                        }),
                        UIEngine::Tags::div({{"class", "stat-box"}}, {
                            UIEngine::Tags::span({{"class", "stat-label"}}, "LONGEST WORD"),
                            UIEngine::Tags::span({{"class", "stat-value"}}, longestWord)
                        })
                    })
                });
            }

            [[nodiscard]] UIEngine::VNode render() const override {
                const Json::Array& wordList = _words.get();

                return UIEngine::Tags::main({{"id", "app"}}, {
                    TopBar({
                        {"title", "Lorikeet"},
                        {"count", std::to_string(wordList.size())}
                    }),
                    UIEngine::Router({
                        {"/", [this, &wordList] { return renderDeckView(wordList); }},
                        {"/deck", [this, &wordList] { return renderDeckView(wordList); }},
                        {"/practice", [this, &wordList] { return renderPracticeView(wordList); }},
                        {"/stats", [this, &wordList] { return renderStatsView(wordList); }}
                    }, "/deck")
                });
            }

            [[nodiscard]] UIEngine::StyleSheet styles() const override {
                UIEngine::StyleSheet s;
                s.rule("*", {
                    {"box-sizing", "border-box"}
                })
                .rule("body", {
                    {"margin", "0"},
                    {"font-family", "'Nunito', 'Segoe UI', -apple-system, BlinkMacSystemFont, Roboto, sans-serif"},
                    {"background", "#fbfbfb"},
                    {"color", "#3c3c3c"},
                    {"padding", "2.25rem 1.25rem"}
                })
                .rule("#root", {
                    {"max-width", "760px"},
                    {"margin", "0 auto"}
                })
                .rule(".goals-section", {
                    {"display", "flex"},
                    {"gap", "1rem"},
                    {"margin-bottom", "2rem"},
                    {"flex-wrap", "wrap"}
                })
                .rule(".deck-heading", {
                    {"font-size", "1.3rem"},
                    {"font-weight", "800"},
                    {"color", "#3c3c3c"},
                    {"margin", "0 0 1rem 0"}
                })
                .rule(".cards-grid", {
                    {"display", "grid"},
                    {"grid-template-columns", "repeat(auto-fill, minmax(210px, 1fr))"},
                    {"gap", "1rem"}
                })
                .rule(".stats-grid", {
                    {"display", "grid"},
                    {"grid-template-columns", "repeat(auto-fill, minmax(210px, 1fr))"},
                    {"gap", "1rem"}
                })
                .rule(".stat-box", {
                    {"background", "#ffffff"},
                    {"border", "2px solid #e5e5e5"},
                    {"border-bottom", "5px solid #e5e5e5"},
                    {"border-radius", "1.15rem"},
                    {"padding", "1.25rem"},
                    {"display", "flex"},
                    {"flex-direction", "column"},
                    {"gap", "0.4rem"}
                })
                .rule(".stat-label", {
                    {"font-size", "0.75rem"},
                    {"font-weight", "800"},
                    {"color", "#777777"},
                    {"letter-spacing", "0.5px"}
                })
                .rule(".stat-value", {
                    {"font-size", "1.55rem"},
                    {"font-weight", "800"},
                    {"color", "#1cb0f6"}
                })
                .rule(".empty-state", {
                    {"grid-column", "1 / -1"},
                    {"text-align", "center"},
                    {"background", "#ffffff"},
                    {"border", "2px dashed #e5e5e5"},
                    {"border-radius", "1.25rem"},
                    {"padding", "2.5rem 1.5rem"}
                })
                .rule(".empty-title", {
                    {"margin", "0 0 0.35rem 0"},
                    {"font-size", "1.15rem"},
                    {"font-weight", "800"},
                    {"color", "#3c3c3c"}
                })
                .rule(".empty-sub", {
                    {"margin", "0"},
                    {"color", "#777777"},
                    {"font-weight", "600"}
                });

                s.merge(TopBar{}.styles());
                s.merge(Counter{}.styles());
                s.merge(AddWordCard{}.styles());
                s.merge(WordCard{}.styles());
                s.merge(PracticeView{}.styles());
                return s;
            }

        private:
            UIEngine::State<Json::Array> _words{"App.words", Json::Array{}}; // Persistent React-style state holding the list of flashcards
    };
}

int main() {
    Server server(8000, "Backend");
    Database db("database.json", true);
    Server frontend(8080, "Frontend");

    if (!db.hasTable("words")) {
        db.createTable("words", Json::Object{
            {"id", Json::Object{{"type", "integer"}, {"primary_key", true}, {"auto_increment", true}}},
            {"word", Json::Object{{"type", "string"}, {"required", true}}},
            {"translation", Json::Object{{"type", "string"}, {"required", true}}}
        });
    }

    if (db.select("words").empty()) {
        db.insert("words", Json::Object{{"word", "Hola"}, {"translation", "Hello"}});
        db.insert("words", Json::Object{{"word", "Gracias"}, {"translation", "Thank you"}});
        db.insert("words", Json::Object{{"word", "Bonjour"}, {"translation", "Good morning"}});
    }

    server.get("/words", [&server, &db](const ServerRequest&, ServerResponse& res) {
        server.sendJson(res, HTTP_OK, db.select("words"));
    });

    server.post("/words", [&server, &db](const ServerRequest& req, ServerResponse& res) {
        const Json body = Json::parse(req.body);
        if (!body.isObject()) {
            server.sendJson(res, HTTP_BAD_REQUEST, Json::Object{{"error", "Expected JSON object"}});
            return;
        }
        const Json::Object& obj = body.asObject();
        if (!obj.contains("word") || !obj.contains("translation")) {
            server.sendJson(res, HTTP_BAD_REQUEST, Json::Object{{"error", "Missing 'word' or 'translation'"}});
            return;
        }

        const Json::Object inserted = db.insert("words", Json::Object{
            {"word", obj.at("word").asString()},
            {"translation", obj.at("translation").asString()}
        });
        server.sendJson(res, HTTP_CREATED, inserted);
    });

    server.post("/words/delete", [&server, &db](const ServerRequest& req, ServerResponse& res) {
        if (!req.hasQuery("id")) {
            server.sendJson(res, HTTP_BAD_REQUEST, Json::Object{{"error", "Missing 'id' query parameter"}});
            return;
        }
        const long long id = std::stoll(req.getQuery("id"));
        const Json::Array removed = db.remove("words", Json::Object{{"id", id}});
        server.sendJson(res, HTTP_OK, Json::Object{{"removed", static_cast<int>(removed.size())}});
    });

    UIEngine::serveApp(frontend, App{}, {}, "http://localhost:8000", "Lorikeet");

    std::thread serverThread = server.startAsync();
    std::thread frontendThread = frontend.startAsync();

    serverThread.join();
    frontendThread.join();

    return 0;
}
