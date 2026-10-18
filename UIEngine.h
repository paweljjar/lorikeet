//
// Created by PAWEL on 17/10/2026.
//

#ifndef LORIKEET_UIENGINE_H
#define LORIKEET_UIENGINE_H

#include <any>
#include <chrono>
#include <concepts>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "Json.h"
#include "Server.h"

namespace UIEngine {
    struct Event { // Represents a DOM event dispatched from the browser runtime to a C++ event handler
        std::string type; // Event type name (e.g., "click", "submit", "input", "change")
        std::string value; // Target input element value for "input" and "change" events
        std::map<std::string, std::string> form; // Key-value map of submitted form fields for "submit" events
    };

    using EventHandler = std::function<void(const Event&)>; // Callback signature for handling browser DOM events in C++

    class PropValue { // Variant wrapper holding either a string HTML attribute or a C++ event handler callback
        public:
            PropValue() : _value(std::string{}) {} // Default constructor initializing an empty string property
            PropValue(const char* s) : _value(std::string(s ? s : "")) {} // Implicit constructor from C-string literal
            PropValue(std::string s) : _value(std::move(s)) {} // Implicit constructor from std::string
            PropValue(EventHandler handler) : _value(std::move(handler)) {} // Implicit constructor from an EventHandler function

            template <typename Fn>
            requires (!std::same_as<std::decay_t<Fn>, PropValue> &&
                      !std::convertible_to<Fn, std::string> &&
                      std::invocable<Fn, const Event&>)
            PropValue(Fn&& fn) : _value(EventHandler(std::forward<Fn>(fn))) {} // Implicit constructor from any callable accepting const Event&

            template <typename Fn>
            requires (!std::same_as<std::decay_t<Fn>, PropValue> &&
                      !std::convertible_to<Fn, std::string> &&
                      !std::invocable<Fn, const Event&> &&
                      std::invocable<Fn>)
            PropValue(Fn&& fn) : _value(EventHandler([f = std::forward<Fn>(fn)](const Event&) { f(); })) {} // Implicit constructor from any zero-argument callable

            [[nodiscard]] bool isString() const { return std::holds_alternative<std::string>(_value); } // Check whether this property stores a string value
            [[nodiscard]] bool isHandler() const { return std::holds_alternative<EventHandler>(_value); } // Check whether this property stores an event handler callback
            [[nodiscard]] const std::string& asString() const { return std::get<std::string>(_value); } // Access the underlying string value
            [[nodiscard]] const EventHandler& asHandler() const { return std::get<EventHandler>(_value); } // Access the underlying event handler callback

        private:
            std::variant<std::string, EventHandler> _value; // Internal variant storing either a string attribute or an EventHandler
    };

    using Props = std::map<std::string, PropValue>; // Key-value map representing HTML attributes and C++ event handlers
    using StyleDeclarations = std::map<std::string, std::string>; // Key-value map of CSS property names to values (e.g., {"background", "#1e293b"})

    struct FetchOptions { // Configuration options for outgoing HTTP requests made via UIEngine::fetch
        std::string method = "GET"; // HTTP request method (e.g., "GET", "POST", "DELETE")
        std::map<std::string, std::string> headers; // Optional custom HTTP headers to include in the request
        HTTPServer::Json body = nullptr; // Optional JSON payload serialized as the request body when non-null
    };

    struct FetchResponse { // Response returned by UIEngine::fetch after completing an HTTP request
        int status = 0; // HTTP response status code (0 if the connection or request failed)
        std::string rawBody; // Raw HTTP response body content

        [[nodiscard]] bool ok() const { return status >= 200 && status < 300; } // Check whether the HTTP status code is in the 2xx success range
        [[nodiscard]] HTTPServer::Json json() const; // Parse the raw response body into an HTTPServer::Json value (or null on failure)
    };

    FetchResponse fetch(const std::string& urlOrEndpoint, const FetchOptions& options = {}); // Execute a synchronous HTTP request against the backend API or a full URL

    struct Session { // Represents an isolated client or browser tab session holding scoped UI state and event handlers
        std::string id; // Unique session identifier token
        mutable std::recursive_mutex mutex; // Mutex guarding concurrent access to this session's state and handlers
        std::map<std::string, std::any> stateStore; // Per-session key-value map storing component state values
        std::map<std::string, EventHandler> handlerRegistry; // Per-session map of generated handler IDs to callbacks
        size_t nextHandlerId = 0; // Monotonically increasing handler ID counter for this session
        std::chrono::steady_clock::time_point lastAccessed = std::chrono::steady_clock::now(); // Timestamp of last client interaction
        bool mounted = false; // Lifecycle flag indicating whether root component onMount has executed for this session

        void touch(); // Update lastAccessed timestamp to current steady clock time
    };

    class SessionManager { // Thread-safe manager for creating, looking up, and expiring client sessions
        public:
            static SessionManager& instance(); // Retrieve the singleton instance of SessionManager

            std::shared_ptr<Session> getOrCreateSession(const std::string& sessionId); // Get existing session by ID or create a new one
            std::shared_ptr<Session> getSession(const std::string& sessionId); // Look up existing session by ID, returning nullptr if absent
            void removeSession(const std::string& sessionId); // Remove and destroy a session by ID
            void setTtl(std::chrono::seconds ttl); // Configure the maximum idle time before an inactive session is pruned
            [[nodiscard]] size_t activeSessionCount(); // Return the number of currently tracked active sessions

        private:
            SessionManager() = default;
            void _pruneExpiredSessionsLocked(); // Prune sessions that have exceeded the configured TTL (must hold _mutex)

            std::mutex _mutex; // Mutex protecting the session registry
            std::chrono::seconds _ttl{1800}; // Idle session time-to-live (defaults to 30 minutes)
            std::map<std::string, std::shared_ptr<Session>> _sessions; // Registry mapping session IDs to Session instances
    };

    class SessionContext { // RAII guard binding a Session pointer to thread-local storage for the duration of a request
        public:
            explicit SessionContext(std::shared_ptr<Session> session); // Set current active session on this thread
            ~SessionContext(); // Restore previously active session on this thread

            SessionContext(const SessionContext&) = delete;
            SessionContext& operator=(const SessionContext&) = delete;

            [[nodiscard]] static std::shared_ptr<Session> getCurrentSession(); // Retrieve the Session active on the calling thread
            [[nodiscard]] static std::string getCurrentSessionId(); // Retrieve the session ID active on the calling thread

        private:
            std::shared_ptr<Session> _prevSession; // Previously bound session restored on destruction
            std::string _prevId; // Previously bound session ID restored on destruction
    };

    class Component; // Forward declaration for Component-scoped State handles

    namespace Detail { // Internal state store helpers backing persistent UIEngine::State<T> instances across renders
        void pushScope(std::string scopeName); // Push a component scope identifier onto the thread-local scope stack
        void popScope(); // Pop the most recent component scope identifier from the thread-local scope stack
        [[nodiscard]] std::string getCurrentScope(); // Retrieve the current fully-qualified component scope path
        [[nodiscard]] std::string resolveStateKey(const std::string& rawKey); // Resolve a state key against the active component scope
        [[nodiscard]] std::string generateComponentKey(const std::string& typeHint, const std::string& explicitKey = ""); // Generate a scoped component identifier
        void resetRenderContext(); // Reset the render pass component indexing and scope stack

        class ComponentScopeGuard { // RAII guard pushing and popping a component scope during rendering
            public:
                explicit ComponentScopeGuard(std::string scope) {
                    pushScope(std::move(scope));
                }
                ~ComponentScopeGuard() {
                    popScope();
                }
                ComponentScopeGuard(const ComponentScopeGuard&) = delete;
                ComponentScopeGuard& operator=(const ComponentScopeGuard&) = delete;
        };

        std::any& getOrInitState(const std::string& key, const std::function<std::any()>& initializer); // Retrieve a reference to the stored std::any value for key, initializing it if absent
        void setStateValue(const std::string& key, std::any value); // Overwrite the stored std::any value for key in the persistent state store
    }

    template <typename T>
    class State { // React-style persistent component state handle backed by the server-side state store
        public:
            explicit State(const std::string &key, T initialValue = {})
                : _key(Detail::resolveStateKey(key)), _initialValue(std::move(initialValue)) { // Register or look up the persistent state slot by key
                Detail::getOrInitState(_key, [init = _initialValue] { return std::any(init); });
            }

            State(const Component* owner, const std::string &key, T initialValue = {}); // Construct a state handle explicitly bound to a component instance scope

            [[nodiscard]] const std::string& key() const { return _key; } // Return the fully resolved state key
            [[nodiscard]] const T& get() const { // Read the current value stored for this state key
                return std::any_cast<const T&>(Detail::getOrInitState(_key, [init = _initialValue] { return std::any(init); }));
            }

            void set(T newValue) const { // Update the stored value for this state key (usable inside const render() lambdas)
                Detail::setStateValue(_key, std::any(std::move(newValue)));
            }

            operator const T&() const { return get(); } // Implicitly convert the state handle to a const reference of its underlying value
            const State& operator=(T newValue) const { set(std::move(newValue)); return *this; } // Assign a new value to the state slot

        private:
            std::string _key; // Unique identifier key for this state slot in the persistent state store
            T _initialValue; // Initial value stored to lazily initialize per-session state slots
    };

    class StyleSheet { // CSS-in-C++ stylesheet builder that stores selector rules and serializes them into a CSS document
        public:
            StyleSheet() = default; // Default constructor creating an empty stylesheet

            StyleSheet& rule(std::string selector, StyleDeclarations declarations); // Add or merge CSS declarations for a given CSS selector
            void merge(const StyleSheet& other); // Merge all rules from another StyleSheet into this one
            [[nodiscard]] bool empty() const; // Check whether the stylesheet contains any rules
            [[nodiscard]] std::string toCss() const; // Serialize all rules and declarations into a valid CSS string

        private:
            std::vector<std::pair<std::string, StyleDeclarations>> _rules; // Ordered list of CSS selector rules preserving insertion order
    };

    struct VNode { // Virtual DOM node representing either an HTML element or a raw text node
        std::string tag; // HTML tag name (e.g., "div", "button") or "#text" for raw text nodes
        Props props; // Map of HTML attributes and properties attached to this element
        std::string text; // Text content used when tag is "#text"
        std::vector<VNode> children; // Ordered list of child Virtual DOM nodes
        std::string key = ""; // Optional unique reconciliation key for keyed DOM diffing

        static VNode textNode(std::string content); // Create a raw text VNode with tag "#text" and the given text content
        static VNode el(std::string tag, Props props = {}, std::vector<VNode> children = {}); // Create an HTML element VNode with tag, attributes, and child nodes

        [[nodiscard]] HTTPServer::Json toJson() const; // Recursively serialize this VNode and its children into an HTTPServer::Json object
    };

    class Component { // Abstract base class for object-oriented UI components with props, state, lifecycle, and a pure virtual render() method
        public:
            explicit Component(Props props = {}) : _props(std::move(props)) {} // Construct a component with an optional map of properties
            virtual ~Component() = default; // Virtual destructor for safe polymorphic cleanup of derived components

            virtual void onMount() {} // Optional lifecycle hook invoked once when the component is initially mounted
            [[nodiscard]] virtual VNode render() const = 0; // Pure virtual method implemented by derived components to return their VNode subtree
            [[nodiscard]] virtual StyleSheet styles() const { return {}; } // Optional virtual method allowing components to define their own scoped/associated CSS rules
            [[nodiscard]] virtual std::string getComponentName() const { return "Component"; } // Optional name hint used for automatic scope generation

            [[nodiscard]] std::string getComponentKey() const; // Determine unique scope identifier for this component instance
            [[nodiscard]] std::string scopeKey(const std::string& key) const; // Resolve a state key prefixed with this component instance scope

            template <typename T>
            [[nodiscard]] State<T> useState(const std::string& key, T initialValue = {}) const { // Ergonomic hook to declare or access scoped state in render()
                return State<T>(scopeKey(key), std::move(initialValue));
            }

            template <typename T>
            [[nodiscard]] State<T> state(const std::string& key, T initialValue = {}) const { // Alias for useState
                return useState<T>(key, std::move(initialValue));
            }

            [[nodiscard]] VNode renderWithScope() const; // Execute render() wrapped inside this component's active scope guard

            operator VNode() const { return renderWithScope(); } // Implicitly convert any Component instance into its rendered VNode for seamless nesting in children lists

        protected:
            Props _props; // Component properties passed in at construction time
            mutable std::string _instanceKey; // Cached instance scope key for this component

            [[nodiscard]] bool hasProp(const std::string& key) const; // Check whether a specific property key was provided to this component
            [[nodiscard]] std::string getProp(const std::string& key, const std::string& fallback = "") const; // Retrieve a string property value by key, or return fallback if absent
            [[nodiscard]] EventHandler getCallback(const std::string& key) const; // Retrieve an EventHandler callback prop by key, or an empty function if absent
            void triggerCallback(const std::string& key, const Event& event = {}) const; // Invoke an EventHandler callback prop by key if present
    };

    template <typename T>
    State<T>::State(const Component* owner, const std::string &key, T initialValue)
        : _key(owner ? owner->scopeKey(key) : Detail::resolveStateKey(key)), _initialValue(std::move(initialValue)) {
        Detail::getOrInitState(_key, [init = _initialValue] { return std::any(init); });
    }

    template <typename CompT, typename... Args>
    requires std::derived_from<CompT, Component>
    VNode component(Args&&... args) { // Helper to instantiate a derived Component with arguments and return its rendered VNode
        CompT instance(std::forward<Args>(args)...); // Construct the component instance with forwarded arguments
        return instance.renderWithScope(); // Render and return the resulting VNode subtree
    }

    // Mount a root VNode factory and StyleSheet onto the frontend HTTP server (registers GET "/", GET "/styles.css", GET "/vdom", and POST "/event")
    void serveApp(
        HTTPServer::Server& frontend,
        std::function<VNode()> rootFactory,
        const StyleSheet &styleSheet = {},
        std::string apiBaseUrl = "http://localhost:8000",
        const std::string &pageTitle = "App"
    );

    // Overload of serveApp that accepts a root Component subclass directly, runs onMount() once, and automatically extracts its styles()
    template <typename CompT>
    requires std::derived_from<CompT, Component>
    void serveApp(
        HTTPServer::Server& frontend,
        CompT rootComponent,
        const StyleSheet& styleSheet = {},
        std::string apiBaseUrl = "http://localhost:8000",
        std::string pageTitle = "App"
    ) {
        StyleSheet combinedStyles = rootComponent.styles(); // Start with styles declared on the root component
        combinedStyles.merge(styleSheet); // Merge any additional global stylesheet rules passed to serveApp
        serveApp(
            frontend,
            [comp = std::move(rootComponent)]() mutable {
                const auto session = SessionContext::getCurrentSession();
                if (session) {
                    if (!session->mounted) {
                        session->mounted = true;
                        Detail::ComponentScopeGuard mountGuard(comp.getComponentKey());
                        comp.onMount();
                    }
                }
                return comp.renderWithScope();
            },
            std::move(combinedStyles),
            std::move(apiBaseUrl),
            std::move(pageTitle)
        );
    }

    namespace Tags { // Ergonomic HTML tag helper functions for building VNode trees
        VNode div(Props props = {}, std::vector<VNode> children = {}); // Create a <div> container element with props and child VNodes
        VNode span(Props props = {}, std::vector<VNode> children = {}); // Create a <span> inline container element with props and child VNodes
        VNode span(Props props, const std::string &text); // Create a <span> inline element containing a single text node
        VNode header(Props props = {}, std::vector<VNode> children = {}); // Create a <header> semantic element with props and child VNodes
        VNode main(Props props = {}, std::vector<VNode> children = {}); // Create a <main> semantic element with props and child VNodes
        VNode section(Props props = {}, std::vector<VNode> children = {}); // Create a <section> semantic element with props and child VNodes
        VNode ul(Props props = {}, std::vector<VNode> children = {}); // Create an unordered list <ul> element with props and child VNodes
        VNode li(Props props = {}, std::vector<VNode> children = {}); // Create a list item <li> element with props and child VNodes
        VNode form(Props props = {}, std::vector<VNode> children = {}); // Create a <form> element with props and child VNodes

        VNode h1(Props props, std::string text); // Create an <h1> heading element containing a single text node
        VNode h2(Props props, std::string text); // Create an <h2> heading element containing a single text node
        VNode h3(Props props, std::string text); // Create an <h3> heading element containing a single text node
        VNode p(Props props, std::string text); // Create a <p> paragraph element containing a single text node
        VNode button(Props props, std::string text); // Create a <button> element containing a single text label
        VNode input(Props props = {}); // Create a self-closing <input> element with the given attributes
    }
}

#endif // LORIKEET_UIENGINE_H
