//
// Created by PAWEL on 17/10/2026.
//

#include "UIEngine.h"

#include <algorithm>
#include <mutex>
#include <netdb.h>
#include <set>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace UIEngine {
    namespace {
        thread_local std::shared_ptr<Session> currentThreadSession = nullptr; // Active Session bound to the current executing thread
        thread_local std::string currentThreadSessionId; // Active session ID string bound to the current executing thread

        thread_local std::vector<std::string> threadScopeStack; // Thread-local component hierarchy scope stack
        thread_local std::map<std::string, size_t> threadChildIndexMap; // Thread-local component occurrence counters per render pass
        thread_local std::set<std::string> threadVisitedComponents; // Thread-local set of component keys rendered during the current pass

        std::mutex fallbackStateMutex; // Mutex guarding fallback state and mounted component maps when running outside an active session context
        std::map<std::string, std::any> fallbackStateStore; // Fallback state storage for non-session or startup state access
        std::map<std::string, std::function<void()>> fallbackMountedComponents; // Fallback registry of mounted components outside an active session

        std::mutex fallbackHandlerMutex; // Mutex guarding fallback handler registry
        std::map<std::string, EventHandler> fallbackHandlerRegistry; // Fallback handler registry
        size_t fallbackNextHandlerId = 0; // Fallback handler ID counter

        std::string configuredApiBaseUrl = "http://localhost:8000"; // Default base URL used to resolve relative endpoints in UIEngine::fetch

        void sortUnmountOrder(std::vector<std::string>& keys) { // Sort component scope keys deepest-first so children unmount before parents
            std::sort(keys.begin(), keys.end(), [](const std::string& a, const std::string& b) {
                const auto depthA = std::count(a.begin(), a.end(), '/');
                const auto depthB = std::count(b.begin(), b.end(), '/');
                if (depthA != depthB) {
                    return depthA > depthB;
                }
                if (a.size() != b.size()) {
                    return a.size() > b.size();
                }
                return a > b;
            });
        }

        void eraseScopedStateForKey(std::map<std::string, std::any>& store, const std::string& compKey) { // Remove all state entries owned by compKey
            const std::string prefix = compKey + ".";
            for (auto it = store.begin(); it != store.end(); ) {
                if (it->first == compKey || it->first.starts_with(prefix)) {
                    it = store.erase(it);
                } else {
                    ++it;
                }
            }
        }

        std::string normalizeEventAttr(const std::string& propName) { // Map C++ event prop names (e.g., "onClick") to DOM data attributes (e.g., "data-on-click")
            if (propName == "onClick" || propName == "onclick" || propName == "data-on-click") {
                return "data-on-click";
            }
            if (propName == "onSubmit" || propName == "onsubmit" || propName == "data-on-submit") {
                return "data-on-submit";
            }
            if (propName == "onInput" || propName == "oninput" || propName == "data-on-input") {
                return "data-on-input";
            }
            if (propName == "onChange" || propName == "onchange" || propName == "data-on-change") {
                return "data-on-change";
            }
            return propName;
        }

        std::string registerEventHandler(const EventHandler& handler) { // Register an EventHandler for the current render pass and return its generated ID
            const auto session = SessionContext::getCurrentSession();
            if (session) {
                std::lock_guard lock(session->mutex);
                const std::string id = "h_" + std::to_string(session->nextHandlerId++);
                session->handlerRegistry[id] = handler;
                return id;
            }
            std::lock_guard lock(fallbackHandlerMutex);
            const std::string id = "h_" + std::to_string(fallbackNextHandlerId++);
            fallbackHandlerRegistry[id] = handler;
            return id;
        }

        HTTPServer::Json renderRootVNode(const std::function<VNode()>& rootFactory) { // Clear handler registry, render the root VNode tree, reconcile unmounts, and serialize to JSON
            Detail::resetRenderContext();
            const auto session = SessionContext::getCurrentSession();
            if (session) {
                std::lock_guard lock(session->mutex);
                session->handlerRegistry.clear();
                session->nextHandlerId = 0;
                VNode rootNode = rootFactory();
                if (Detail::reconcileUnmountedComponents()) {
                    Detail::resetRenderContext();
                    session->handlerRegistry.clear();
                    session->nextHandlerId = 0;
                    rootNode = rootFactory();
                    Detail::reconcileUnmountedComponents();
                }
                return rootNode.toJson();
            }
            std::lock_guard lock(fallbackHandlerMutex);
            fallbackHandlerRegistry.clear();
            fallbackNextHandlerId = 0;
            VNode rootNode = rootFactory();
            if (Detail::reconcileUnmountedComponents()) {
                Detail::resetRenderContext();
                fallbackHandlerRegistry.clear();
                fallbackNextHandlerId = 0;
                rootNode = rootFactory();
                Detail::reconcileUnmountedComponents();
            }
            return rootNode.toJson();
        }

        struct ParsedUrl { // Decomposed HTTP URL components for establishing a TCP socket connection
            std::string host = "localhost"; // Target hostname or IPv4/IPv6 address
            std::string port = "80"; // Target TCP port string
            std::string path = "/"; // Request path including optional query string
        };

        ParsedUrl parseHttpUrl(const std::string& rawUrl) { // Parse a full or relative URL into host, port, and path components
            std::string fullUrl = rawUrl;
            if (!fullUrl.starts_with("http://") && !fullUrl.starts_with("https://")) {
                std::string base = configuredApiBaseUrl;
                if (!base.empty() && base.back() == '/') {
                    base.pop_back();
                }
                fullUrl = base + (fullUrl.starts_with("/") ? fullUrl : "/" + fullUrl);
            }

            if (fullUrl.starts_with("http://")) {
                fullUrl = fullUrl.substr(7);
            } else if (fullUrl.starts_with("https://")) {
                fullUrl = fullUrl.substr(8);
            }

            ParsedUrl out;
            const size_t slashPos = fullUrl.find('/');
            std::string authority = slashPos == std::string::npos ? fullUrl : fullUrl.substr(0, slashPos);
            out.path = slashPos == std::string::npos ? "/" : fullUrl.substr(slashPos);

            const size_t colonPos = authority.find(':');
            if (colonPos != std::string::npos) {
                out.host = authority.substr(0, colonPos);
                out.port = authority.substr(colonPos + 1);
            } else {
                out.host = authority;
                out.port = "80";
            }
            return out;
        }
    }

    void Session::touch() { // Update lastAccessed timestamp to current steady clock time
        lastAccessed = std::chrono::steady_clock::now();
    }

    SessionManager& SessionManager::instance() { // Retrieve the singleton instance of SessionManager
        static SessionManager mgr;
        return mgr;
    }

    std::shared_ptr<Session> SessionManager::getOrCreateSession(const std::string& sessionId) { // Get existing session by ID or create a new one
        std::lock_guard lock(_mutex);
        _pruneExpiredSessionsLocked();

        const auto it = _sessions.find(sessionId);
        if (it != _sessions.end()) {
            it->second->touch();
            return it->second;
        }

        auto session = std::make_shared<Session>();
        session->id = sessionId;
        session->touch();
        _sessions[sessionId] = session;
        return session;
    }

    std::shared_ptr<Session> SessionManager::getSession(const std::string& sessionId) { // Look up existing session by ID, returning nullptr if absent
        std::lock_guard lock(_mutex);
        const auto it = _sessions.find(sessionId);
        if (it != _sessions.end()) {
            it->second->touch();
            return it->second;
        }
        return nullptr;
    }

    void SessionManager::removeSession(const std::string& sessionId) { // Remove and destroy a session by ID after unmounting its components
        std::shared_ptr<Session> removedSession;
        {
            std::lock_guard lock(_mutex);
            const auto it = _sessions.find(sessionId);
            if (it != _sessions.end()) {
                removedSession = it->second;
                _sessions.erase(it);
            }
        }
        if (removedSession) {
            SessionContext sessionCtx(removedSession);
            Detail::unmountAllComponents(*removedSession);
        }
    }

    void SessionManager::setTtl(const std::chrono::seconds ttl) { // Configure the maximum idle time before an inactive session is pruned
        std::lock_guard lock(_mutex);
        _ttl = ttl;
    }

    size_t SessionManager::activeSessionCount() { // Return the number of currently tracked active sessions
        std::lock_guard lock(_mutex);
        return _sessions.size();
    }

    void SessionManager::_pruneExpiredSessionsLocked() { // Prune sessions that have exceeded the configured TTL (must hold _mutex)
        const auto now = std::chrono::steady_clock::now();
        for (auto it = _sessions.begin(); it != _sessions.end(); ) {
            if (now - it->second->lastAccessed > _ttl) {
                std::shared_ptr<Session> expiredSession = it->second;
                it = _sessions.erase(it);
                SessionContext sessionCtx(expiredSession);
                Detail::unmountAllComponents(*expiredSession);
            } else {
                ++it;
            }
        }
    }

    SessionContext::SessionContext(std::shared_ptr<Session> session) // Set current active session on this thread
        : _prevSession(currentThreadSession), _prevId(currentThreadSessionId) {
        currentThreadSession = std::move(session);
        currentThreadSessionId = currentThreadSession ? currentThreadSession->id : "";
        if (currentThreadSession) {
            currentThreadSession->touch();
        }
    }

    SessionContext::~SessionContext() { // Restore previously active session on this thread
        currentThreadSession = std::move(_prevSession);
        currentThreadSessionId = std::move(_prevId);
    }

    std::shared_ptr<Session> SessionContext::getCurrentSession() { // Retrieve the Session active on the calling thread
        return currentThreadSession;
    }

    std::string SessionContext::getCurrentSessionId() { // Retrieve the session ID active on the calling thread
        return currentThreadSessionId;
    }

    HTTPServer::Json FetchResponse::json() const { // Parse the raw response body into an HTTPServer::Json value (or null on failure)
        if (rawBody.empty()) {
            return nullptr;
        }
        try {
            return HTTPServer::Json::parse(rawBody);
        } catch (...) {
            return nullptr;
        }
    }

    FetchResponse fetch(const std::string& urlOrEndpoint, const FetchOptions& options) { // Execute a synchronous HTTP request against the backend API or a full URL
        const auto [host, port, path] = parseHttpUrl(urlOrEndpoint);

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* res = nullptr;
        if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) {
            return FetchResponse{.status = 0, .rawBody = ""};
        }

        int sock = -1;
        for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
            sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (sock < 0) continue;
            if (connect(sock, p->ai_addr, p->ai_addrlen) == 0) {
                break;
            }
            close(sock);
            sock = -1;
        }
        freeaddrinfo(res);

        if (sock < 0) {
            return FetchResponse{.status = 0, .rawBody = ""};
        }

        const std::string serializedBody = options.body.isNull() ? "" : options.body.dump();
        std::ostringstream reqStream;
        reqStream << options.method << " " << path << " HTTP/1.1\r\n";
        reqStream << "Host: " << host << ":" << port << "\r\n";
        reqStream << "Connection: close\r\n";

        bool hasContentType = false;
        for (const auto& [k, v] : options.headers) {
            if (k == "Content-Type" || k == "content-type") {
                hasContentType = true;
            }
            reqStream << k << ": " << v << "\r\n";
        }

        if (!serializedBody.empty()) {
            if (!hasContentType) {
                reqStream << "Content-Type: application/json\r\n";
            }
            reqStream << "Content-Length: " << serializedBody.size() << "\r\n";
        } else if (options.method == "POST" || options.method == "PUT" || options.method == "PATCH") {
            reqStream << "Content-Length: 0\r\n";
        }

        reqStream << "\r\n" << serializedBody;
        const std::string rawRequest = reqStream.str();

        size_t totalSent = 0;
        while (totalSent < rawRequest.size()) {
            const ssize_t sent = send(sock, rawRequest.data() + totalSent, rawRequest.size() - totalSent, 0);
            if (sent <= 0) {
                close(sock);
                return FetchResponse{.status = 0, .rawBody = ""};
            }
            totalSent += static_cast<size_t>(sent);
        }

        std::string rawResponse;
        char buffer[4096];
        while (true) {
            const ssize_t n = recv(sock, buffer, sizeof(buffer), 0);
            if (n <= 0) break;
            rawResponse.append(buffer, static_cast<size_t>(n));
        }
        close(sock);

        FetchResponse response;
        const size_t headerEnd = rawResponse.find("\r\n\r\n");
        if (headerEnd == std::string::npos) {
            return response;
        }

        const std::string headerBlock = rawResponse.substr(0, headerEnd);
        response.rawBody = rawResponse.substr(headerEnd + 4);

        std::istringstream headerStream(headerBlock);
        std::string httpVersion;
        headerStream >> httpVersion >> response.status;
        return response;
    }

    namespace Detail {
        void pushScope(std::string scopeName) { // Push a component scope identifier onto the thread-local scope stack
            threadScopeStack.push_back(std::move(scopeName));
        }

        void popScope() { // Pop the most recent component scope identifier from the thread-local scope stack
            if (!threadScopeStack.empty()) {
                threadScopeStack.pop_back();
            }
        }

        std::string getCurrentScope() { // Retrieve the current fully-qualified component scope path
            return threadScopeStack.empty() ? "" : threadScopeStack.back();
        }

        std::string resolveStateKey(const std::string& rawKey) { // Resolve a state key against active component scope
            if (rawKey.empty()) {
                return rawKey;
            }
            if (rawKey.front() == '@' || rawKey.front() == '/') {
                return rawKey.substr(1);
            }
            const std::string scope = getCurrentScope();
            if (scope.empty() || rawKey.starts_with(scope + ".")) {
                return rawKey;
            }
            return scope + "." + rawKey;
        }

        std::string generateComponentKey(const std::string& typeHint, const std::string& explicitKey) { // Generate a scoped component identifier
            const std::string parentScope = getCurrentScope();
            std::string childKey;
            if (!explicitKey.empty()) {
                childKey = typeHint + ":" + explicitKey;
            } else {
                const std::string counterKey = parentScope.empty() ? typeHint : parentScope + "/" + typeHint;
                const size_t index = threadChildIndexMap[counterKey]++;
                childKey = typeHint + "#" + std::to_string(index);
            }
            return parentScope.empty() ? childKey : parentScope + "/" + childKey;
        }

        void resetRenderContext() { // Reset the render pass component indexing, scope stack, and visited components set
            threadScopeStack.clear();
            threadChildIndexMap.clear();
            threadVisitedComponents.clear();
        }

        void registerComponentMount(const std::string& key, Component& comp, std::function<void()> unmountFn) { // Record component in current render pass and invoke onMount() if newly mounted
            threadVisitedComponents.insert(key);
            const auto session = SessionContext::getCurrentSession();
            if (session) {
                std::lock_guard lock(session->mutex);
                const bool isNew = !session->mountedComponents.contains(key);
                session->mountedComponents[key] = std::move(unmountFn);
                if (isNew) {
                    comp.onMount();
                }
                return;
            }
            std::lock_guard lock(fallbackStateMutex);
            const bool isNew = !fallbackMountedComponents.contains(key);
            fallbackMountedComponents[key] = std::move(unmountFn);
            if (isNew) {
                comp.onMount();
            }
        }

        bool reconcileUnmountedComponents() { // Invoke onUnmount() and clean up scoped state for components that disappeared in the latest render pass
            const auto session = SessionContext::getCurrentSession();
            if (session) {
                std::lock_guard lock(session->mutex);
                std::vector<std::string> unmountedKeys;
                for (const auto& [key, _] : session->mountedComponents) {
                    if (!threadVisitedComponents.contains(key)) {
                        unmountedKeys.push_back(key);
                    }
                }
                if (unmountedKeys.empty()) {
                    return false;
                }
                sortUnmountOrder(unmountedKeys);
                for (const std::string& key : unmountedKeys) {
                    const auto it = session->mountedComponents.find(key);
                    if (it != session->mountedComponents.end()) {
                        std::function<void()> unmountFn = std::move(it->second);
                        session->mountedComponents.erase(it);
                        if (unmountFn) {
                            ComponentScopeGuard guard(key);
                            unmountFn();
                        }
                    }
                    eraseScopedStateForKey(session->stateStore, key);
                }
                return true;
            }

            std::lock_guard lock(fallbackStateMutex);
            std::vector<std::string> unmountedKeys;
            for (const auto& [key, _] : fallbackMountedComponents) {
                if (!threadVisitedComponents.contains(key)) {
                    unmountedKeys.push_back(key);
                }
            }
            if (unmountedKeys.empty()) {
                return false;
            }
            sortUnmountOrder(unmountedKeys);
            for (const std::string& key : unmountedKeys) {
                const auto it = fallbackMountedComponents.find(key);
                if (it != fallbackMountedComponents.end()) {
                    std::function<void()> unmountFn = std::move(it->second);
                    fallbackMountedComponents.erase(it);
                    if (unmountFn) {
                        ComponentScopeGuard guard(key);
                        unmountFn();
                    }
                }
                eraseScopedStateForKey(fallbackStateStore, key);
            }
            return true;
        }

        void unmountAllComponents(Session& session) { // Invoke onUnmount() for all mounted components in the given session and clear its lifecycle registry
            std::lock_guard lock(session.mutex);
            std::vector<std::string> allKeys;
            allKeys.reserve(session.mountedComponents.size());
            for (const auto& [key, _] : session.mountedComponents) {
                allKeys.push_back(key);
            }
            sortUnmountOrder(allKeys);
            for (const std::string& key : allKeys) {
                const auto it = session.mountedComponents.find(key);
                if (it != session.mountedComponents.end()) {
                    std::function<void()> unmountFn = std::move(it->second);
                    session.mountedComponents.erase(it);
                    if (unmountFn) {
                        ComponentScopeGuard guard(key);
                        unmountFn();
                    }
                }
            }
            session.mountedComponents.clear();
        }

        std::any& getOrInitState(const std::string& key, const std::function<std::any()>& initializer) { // Retrieve a reference to the stored std::any value for key, initializing it if absent
            const auto session = SessionContext::getCurrentSession();
            if (session) {
                std::lock_guard lock(session->mutex);
                auto it = session->stateStore.find(key);
                if (it == session->stateStore.end()) {
                    it = session->stateStore.emplace(key, initializer()).first;
                }
                return it->second;
            }
            std::lock_guard lock(fallbackStateMutex);
            auto it = fallbackStateStore.find(key);
            if (it == fallbackStateStore.end()) {
                it = fallbackStateStore.emplace(key, initializer()).first;
            }
            return it->second;
        }

        void setStateValue(const std::string& key, std::any value) { // Overwrite the stored std::any value for key in the persistent state store
            const auto session = SessionContext::getCurrentSession();
            if (session) {
                std::lock_guard lock(session->mutex);
                session->stateStore[key] = std::move(value);
                return;
            }
            std::lock_guard lock(fallbackStateMutex);
            fallbackStateStore[key] = std::move(value);
        }
    }

    StyleSheet& StyleSheet::rule(std::string selector, StyleDeclarations declarations) { // Add or merge CSS declarations for a given CSS selector
        for (auto& [existingSelector, existingDecls] : _rules) { // Search if the selector already exists in the stylesheet
            if (existingSelector == selector) { // If a matching selector rule exists, merge or overwrite its declarations
                for (auto& [prop, val] : declarations) {
                    existingDecls[prop] = std::move(val);
                }
                return *this;
            }
        }
        _rules.emplace_back(std::move(selector), std::move(declarations)); // Otherwise append a new selector rule
        return *this; // Return reference to *this for fluent method chaining
    }

    void StyleSheet::merge(const StyleSheet& other) { // Merge all rules from another StyleSheet into this one
        for (const auto& [selector, declarations] : other._rules) {
            rule(selector, declarations);
        }
    }

    bool StyleSheet::empty() const { // Check whether the stylesheet contains any rules
        return _rules.empty();
    }

    std::string StyleSheet::toCss() const { // Serialize all rules and declarations into a valid CSS string
        std::ostringstream css;
        for (const auto& [selector, declarations] : _rules) {
            css << selector << " {\n";
            for (const auto& [prop, val] : declarations) {
                css << "    " << prop << ": " << val << ";\n";
            }
            css << "}\n\n";
        }
        return css.str();
    }

    VNode VNode::textNode(std::string content) { // Create a raw text VNode with tag "#text" and the given text content
        return VNode{.tag = "#text", .props = {}, .text = std::move(content), .children = {}, .key = ""}; // Initialize VNode with "#text" tag, empty props, moved text, and no children
    }

    VNode VNode::el(std::string tag, Props props, std::vector<VNode> children) { // Create an HTML element VNode with tag, attributes, and child nodes
        std::string extractedKey; // Extract reconciliation key from props if provided as a string
        const auto it = props.find("key");
        if (it != props.end()) {
            if (it->second.isString()) {
                extractedKey = it->second.asString();
            }
            props.erase(it); // Remove "key" so it is not emitted as a DOM HTML attribute
        }
        return VNode{.tag = std::move(tag), .props = std::move(props), .text = "", .children = std::move(children), .key = std::move(extractedKey)}; // Initialize VNode with moved tag, props, empty text, moved children, and extracted key
    }

    HTTPServer::Json VNode::toJson() const { // Recursively serialize this VNode and its children into an HTTPServer::Json object
        HTTPServer::Json::Object propsObj; // Create a JSON object map to hold this node's HTML attributes and event handler IDs
        for (const auto& [propKey, val] : props) { // Iterate through each key-value pair in the VNode's Props map
            if (propKey == "key") { // Skip reconciliation key if present in props
                continue;
            }
            if (val.isString()) {
                propsObj[propKey] = val.asString(); // Insert the attribute name and string value into the JSON object
            } else if (val.isHandler()) {
                propsObj[normalizeEventAttr(propKey)] = registerEventHandler(val.asHandler()); // Register C++ callback and emit its handler ID
            }
        }

        HTTPServer::Json::Array childrenArr; // Create a JSON array to hold serialized child VNodes
        childrenArr.reserve(children.size()); // Preallocate space for all child VNode JSON objects
        for (const auto& child : children) { // Iterate through each child VNode in order
            childrenArr.push_back(child.toJson()); // Recursively serialize the child VNode and append it to the array
        }

        return HTTPServer::Json::Object{ // Construct and return the complete JSON object representing this VNode
            {"tag", tag},
            {"key", key},
            {"props", std::move(propsObj)},
            {"text", text},
            {"children", std::move(childrenArr)}
        };
    }

    bool Component::hasProp(const std::string& key) const { // Check whether a specific property key was provided to this component
        return _props.contains(key); // Use std::map::contains to check if the key exists in _props
    }

    std::string Component::getProp(const std::string& key, const std::string& fallback) const { // Retrieve a string property value by key, or return fallback if absent
        const auto it = _props.find(key); // Look up the key in the component's _props map
        if (it != _props.end() && it->second.isString()) {
            return it->second.asString(); // Return the found string property value
        }
        return fallback; // Return the fallback string if missing or not a string prop
    }

    EventHandler Component::getCallback(const std::string& key) const { // Retrieve an EventHandler callback prop by key, or an empty function if absent
        const auto it = _props.find(key); // Look up the key in the component's _props map
        if (it != _props.end() && it->second.isHandler()) {
            return it->second.asHandler(); // Return the found EventHandler callback
        }
        return {}; // Return an empty EventHandler if missing or not a handler prop
    }

    void Component::triggerCallback(const std::string& key, const Event& event) const { // Invoke an EventHandler callback prop by key if present
        if (const EventHandler cb = getCallback(key)) {
            cb(event);
        }
    }

    std::string Component::getComponentKey() const { // Determine unique scope identifier for this component instance
        if (!_instanceKey.empty()) {
            return _instanceKey;
        }
        std::string explicitKey;
        if (hasProp("key")) {
            explicitKey = getProp("key");
        } else if (hasProp("id")) {
            explicitKey = getProp("id");
        }
        _instanceKey = Detail::generateComponentKey(getComponentName(), explicitKey);
        return _instanceKey;
    }

    std::string Component::scopeKey(const std::string& key) const { // Resolve a state key prefixed with this component instance scope
        if (key.empty()) {
            return key;
        }
        if (key.front() == '@' || key.front() == '/') {
            return key.substr(1);
        }
        return getComponentKey() + "." + key;
    }

    VNode Component::renderWithScope() const { // Execute render() wrapped inside this component's active scope guard
        const std::string compKey = getComponentKey();
        Detail::ComponentScopeGuard guard(compKey);
        VNode node = render();
        if (node.key.empty()) {
            node.key = compKey;
        }
        return node;
    }

    namespace {
        std::string buildHtmlShell(const std::string& pageTitle) { // Build the minimal HTML shell containing the Virtual DOM renderer and C++ event bridge
            std::ostringstream html;
            html << R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>)HTML" << pageTitle << R"HTML(</title>
    <link rel="stylesheet" href="/styles.css">
</head>
<body>
    <div id="root"></div>
    <script>
        let currentVNode = null;
        let currentSessionId = (window.crypto && window.crypto.randomUUID)
            ? window.crypto.randomUUID().replace(/-/g, "")
            : "tab_" + Math.random().toString(36).substring(2) + Date.now().toString(36);

        function getSessionId() {
            return currentSessionId;
        }

        function syncSessionId(res) {
            const serverSid = res.headers ? res.headers.get("X-Session-Id") : null;
            if (serverSid) {
                currentSessionId = serverSid;
            }
        }

        function renderTree(nextVNode) {
            if (!nextVNode) return;
            const root = document.getElementById("root");
            if (!currentVNode) {
                root.innerHTML = "";
                root.appendChild(createDomNode(nextVNode));
            } else {
                patchDom(root, currentVNode, nextVNode, 0);
            }
            currentVNode = nextVNode;
        }

        async function dispatchEvent(handlerId, eventPayload) {
            try {
                const sid = getSessionId();
                const res = await fetch("/event", {
                    method: "POST",
                    headers: {
                        "Content-Type": "application/json",
                        "X-Session-Id": sid
                    },
                    body: JSON.stringify({ handlerId, ...eventPayload })
                });
                syncSessionId(res);
                if (res.ok) {
                    const nextVNode = await res.json();
                    renderTree(nextVNode);
                } else if (res.status === 404) {
                    const vdomRes = await fetch("/vdom", {
                        headers: { "X-Session-Id": sid }
                    });
                    if (vdomRes.ok) {
                        renderTree(await vdomRes.json());
                    }
                }
            } catch (err) {
                console.error("Event dispatch failed:", err);
            }
        }

        function applyProps(el, oldProps = {}, newProps = {}) {
            for (const key of Object.keys(oldProps)) {
                if (!(key in newProps)) {
                    if (key === "data-on-click") el.onclick = null;
                    else if (key === "data-on-submit") el.onsubmit = null;
                    else if (key === "data-on-input") el.oninput = null;
                    else if (key === "data-on-change") el.onchange = null;
                    else el.removeAttribute(key);
                }
            }
            for (const [key, value] of Object.entries(newProps)) {
                if (oldProps[key] === value) continue;
                if (key === "data-on-submit") {
                    el.onsubmit = async (e) => {
                        e.preventDefault();
                        const form = {};
                        const formData = new FormData(el);
                        for (const [field, val] of formData.entries()) {
                            form[field] = String(val);
                        }
                        await dispatchEvent(value, { type: "submit", form });
                        el.reset();
                    };
                } else if (key === "data-on-click") {
                    el.onclick = async () => {
                        await dispatchEvent(value, { type: "click" });
                    };
                } else if (key === "data-on-input") {
                    el.oninput = async (e) => {
                        await dispatchEvent(value, { type: "input", value: e.target.value || "" });
                    };
                } else if (key === "data-on-change") {
                    el.onchange = async (e) => {
                        await dispatchEvent(value, { type: "change", value: e.target.value || "" });
                    };
                } else if (!key.startsWith("data-on-")) {
                    if (key === "value" && "value" in el) {
                        el.value = value;
                    }
                    el.setAttribute(key, value);
                }
            }
        }

        function createDomNode(vnode) {
            if (vnode.tag === "#text") {
                return document.createTextNode(vnode.text || "");
            }
            const el = document.createElement(vnode.tag);
            applyProps(el, {}, vnode.props || {});
            for (const child of (vnode.children || [])) {
                el.appendChild(createDomNode(child));
            }
            return el;
        }

        function reconcileChildren(parentDom, oldChildren, newChildren) {
            const hasKeys = oldChildren.some(c => c && c.key) || newChildren.some(c => c && c.key);
            if (!hasKeys) {
                while (parentDom.childNodes.length > newChildren.length) {
                    parentDom.removeChild(parentDom.lastChild);
                }
                for (let i = 0; i < newChildren.length; i++) {
                    patchDom(parentDom, oldChildren[i], newChildren[i], i);
                }
                return;
            }

            const existingDomNodes = Array.from(parentDom.childNodes);
            const keyedOldMap = new Map();
            const unkeyedOldIndices = [];

            for (let i = 0; i < oldChildren.length; i++) {
                const oldChild = oldChildren[i];
                const domNode = existingDomNodes[i];
                if (oldChild && oldChild.key) {
                    keyedOldMap.set(oldChild.key, { vnode: oldChild, dom: domNode, index: i });
                } else {
                    unkeyedOldIndices.push(i);
                }
            }

            const matchedOldIndices = new Set();
            let unkeyedCursor = 0;
            const matchedPairs = new Array(newChildren.length);

            for (let i = 0; i < newChildren.length; i++) {
                const newChild = newChildren[i];
                if (newChild && newChild.key) {
                    const entry = keyedOldMap.get(newChild.key);
                    if (entry && !matchedOldIndices.has(entry.index)) {
                        matchedOldIndices.add(entry.index);
                        matchedPairs[i] = entry;
                    } else {
                        matchedPairs[i] = null;
                    }
                } else {
                    while (unkeyedCursor < unkeyedOldIndices.length && matchedOldIndices.has(unkeyedOldIndices[unkeyedCursor])) {
                        unkeyedCursor++;
                    }
                    if (unkeyedCursor < unkeyedOldIndices.length) {
                        const oldIdx = unkeyedOldIndices[unkeyedCursor++];
                        matchedOldIndices.add(oldIdx);
                        matchedPairs[i] = { vnode: oldChildren[oldIdx], dom: existingDomNodes[oldIdx], index: oldIdx };
                    } else {
                        matchedPairs[i] = null;
                    }
                }
            }

            for (let i = 0; i < existingDomNodes.length; i++) {
                if (!matchedOldIndices.has(i) && existingDomNodes[i] && existingDomNodes[i].parentNode === parentDom) {
                    parentDom.removeChild(existingDomNodes[i]);
                }
            }

            for (let i = 0; i < newChildren.length; i++) {
                const newChild = newChildren[i];
                const match = matchedPairs[i];
                const referenceNode = parentDom.childNodes[i] || null;

                if (match && match.dom) {
                    if (match.dom !== referenceNode) {
                        parentDom.insertBefore(match.dom, referenceNode);
                    }
                    patchDom(parentDom, match.vnode, newChild, i);
                } else {
                    const createdDom = createDomNode(newChild);
                    parentDom.insertBefore(createdDom, referenceNode);
                }
            }
        }

        function patchDom(parent, oldVNode, newVNode, index = 0) {
            const existingNode = parent.childNodes[index];
            if (!oldVNode) {
                parent.appendChild(createDomNode(newVNode));
            } else if (!newVNode) {
                if (existingNode) parent.removeChild(existingNode);
            } else if (oldVNode.tag !== newVNode.tag || (oldVNode.key || "") !== (newVNode.key || "")) {
                parent.replaceChild(createDomNode(newVNode), existingNode);
            } else if (newVNode.tag === "#text") {
                if (oldVNode.text !== newVNode.text && existingNode) {
                    existingNode.textContent = newVNode.text;
                }
            } else if (existingNode) {
                applyProps(existingNode, oldVNode.props || {}, newVNode.props || {});
                const oldChildren = oldVNode.children || [];
                const newChildren = newVNode.children || [];
                reconcileChildren(existingNode, oldChildren, newChildren);
            }
        }

        async function initApp() {
            const sid = getSessionId();
            const res = await fetch("/vdom", {
                headers: { "X-Session-Id": sid }
            });
            syncSessionId(res);
            if (res.ok) {
                const initialVNode = await res.json();
                renderTree(initialVNode);
            }
        }

        window.addEventListener("DOMContentLoaded", initApp);
    </script>
</body>
</html>)HTML";
            return html.str();
        }
    }

    void serveApp(
        HTTPServer::Server& frontend,
        std::function<VNode()> rootFactory,
        const StyleSheet &styleSheet,
        std::string apiBaseUrl,
        const std::string &pageTitle
    ) { // Register "/", "/styles.css", "/vdom", and "/event" routes on the frontend server to serve the C++ VDOM app
        configuredApiBaseUrl = std::move(apiBaseUrl); // Store the configured backend base URL for UIEngine::fetch
        const std::string htmlShell = buildHtmlShell(pageTitle); // Pre-build the minimal HTML shell
        const std::string cssContent = styleSheet.toCss(); // Serialize the C++ StyleSheet into a CSS string

        frontend.get("/", [htmlShell](const HTTPServer::ServerRequest& req, HTTPServer::ServerResponse& res) { // Serve the HTML shell at GET /
            const std::string sessionId = HTTPServer::extractOrCreateSessionId(req);
            const auto session = SessionManager::instance().getOrCreateSession(sessionId);
            SessionContext sessionCtx(session);
            res.setHeader("X-Session-Id", sessionId);
            res.setHeader("Set-Cookie", "lorikeet_session=" + sessionId + "; Path=/; SameSite=Lax");
            HTTPServer::Server::sendHtml(res, HTTP_OK, htmlShell);
        });

        frontend.get("/styles.css", [cssContent](const HTTPServer::ServerRequest&, HTTPServer::ServerResponse& res) { // Serve the generated CSS stylesheet at GET /styles.css
            res.status = HTTP_OK;
            res.contentType = "text/css";
            res.charset = "utf-8";
            res.body = cssContent;
        });

        frontend.get("/vdom", [rootFactory](const HTTPServer::ServerRequest& req, HTTPServer::ServerResponse& res) { // Serve the live serialized VNode JSON tree at GET /vdom
            const std::string sessionId = HTTPServer::extractOrCreateSessionId(req);
            const auto session = SessionManager::instance().getOrCreateSession(sessionId);
            SessionContext sessionCtx(session);
            {
                std::lock_guard lock(session->mutex);
                Detail::unmountAllComponents(*session);
                session->stateStore.clear();
            }
            res.setHeader("X-Session-Id", sessionId);
            HTTPServer::Server::sendJson(res, HTTP_OK, renderRootVNode(rootFactory));
        });

        frontend.post("/event", [factory = std::move(rootFactory)](const HTTPServer::ServerRequest& req, HTTPServer::ServerResponse& res) { // Dispatch a browser event to its C++ EventHandler and return the re-rendered VNode tree
            const std::string sessionId = HTTPServer::extractOrCreateSessionId(req);
            const auto session = SessionManager::instance().getOrCreateSession(sessionId);
            SessionContext sessionCtx(session);
            res.setHeader("X-Session-Id", sessionId);

            const HTTPServer::Json payload = HTTPServer::Json::parse(req.body);
            if (!payload.isObject() || !payload.asObject().contains("handlerId")) {
                HTTPServer::Server::sendJson(res, HTTP_BAD_REQUEST, HTTPServer::Json::Object{{"error", "Missing handlerId"}});
                return;
            }

            const HTTPServer::Json::Object& obj = payload.asObject();
            const std::string handlerId = obj.at("handlerId").asString();

            Event event;
            if (obj.contains("type") && obj.at("type").isString()) {
                event.type = obj.at("type").asString();
            }
            if (obj.contains("value") && obj.at("value").isString()) {
                event.value = obj.at("value").asString();
            }
            if (obj.contains("form") && obj.at("form").isObject()) {
                for (const auto& [field, val] : obj.at("form").asObject()) {
                    if (val.isString()) {
                        event.form[field] = val.asString();
                    }
                }
            }

            EventHandler targetHandler;
            {
                std::lock_guard lock(session->mutex);
                const auto it = session->handlerRegistry.find(handlerId);
                if (it != session->handlerRegistry.end()) {
                    targetHandler = it->second;
                }
            }

            if (targetHandler) {
                targetHandler(event);
                HTTPServer::Server::sendJson(res, HTTP_OK, renderRootVNode(factory));
            } else {
                HTTPServer::Server::sendJson(res, HTTP_NOT_FOUND, HTTPServer::Json::Object{{"error", "Unknown or expired handler ID"}});
            }
        });
    }

    namespace Tags {
        VNode div(Props props, std::vector<VNode> children) { // Create a <div> container element with props and child VNodes
            return VNode::el("div", std::move(props), std::move(children));
        }

        VNode span(Props props, std::vector<VNode> children) { // Create a <span> inline container element with props and child VNodes
            return VNode::el("span", std::move(props), std::move(children));
        }

        VNode span(Props props, const std::string &text) { // Create a <span> inline element containing a single text node
            return VNode::el("span", std::move(props), { VNode::textNode(text) });
        }

        VNode header(Props props, std::vector<VNode> children) { // Create a <header> semantic element with props and child VNodes
            return VNode::el("header", std::move(props), std::move(children));
        }

        VNode main(Props props, std::vector<VNode> children) { // Create a <main> semantic element with props and child VNodes
            return VNode::el("main", std::move(props), std::move(children));
        }

        VNode section(Props props, std::vector<VNode> children) { // Create a <section> semantic element with props and child VNodes
            return VNode::el("section", std::move(props), std::move(children));
        }

        VNode ul(Props props, std::vector<VNode> children) { // Create an unordered list <ul> element with props and child VNodes
            return VNode::el("ul", std::move(props), std::move(children));
        }

        VNode li(Props props, std::vector<VNode> children) { // Create a list item <li> element with props and child VNodes
            return VNode::el("li", std::move(props), std::move(children));
        }

        VNode form(Props props, std::vector<VNode> children) { // Create a <form> element with props and child VNodes
            return VNode::el("form", std::move(props), std::move(children));
        }

        VNode h1(Props props, std::string text) { // Create an <h1> heading element containing a single text node
            return VNode::el("h1", std::move(props), {VNode::textNode(std::move(text))});
        }

        VNode h2(Props props, std::string text) { // Create an <h2> heading element containing a single text node
            return VNode::el("h2", std::move(props), {VNode::textNode(std::move(text))});
        }

        VNode h3(Props props, std::string text) { // Create an <h3> heading element containing a single text node
            return VNode::el("h3", std::move(props), {VNode::textNode(std::move(text))});
        }

        VNode p(Props props, std::string text) { // Create a <p> paragraph element containing a single text node
            return VNode::el("p", std::move(props), {VNode::textNode(std::move(text))});
        }

        VNode button(Props props, std::string text) { // Create a <button> element containing a single text label
            return VNode::el("button", std::move(props), {VNode::textNode(std::move(text))});
        }

        VNode input(Props props) { // Create a self-closing <input> element with the given attributes
            return VNode::el("input", std::move(props), {});
        }
    }
}
