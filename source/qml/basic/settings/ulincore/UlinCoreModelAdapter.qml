import QtQml
import ULinHub 1.0

// DSH ModelsPage 的数据适配层：页面结构保持 DSH 原样，只把数据源换成 ULinCore。
QtObject {
    id: adapter

    property var cfg: Hub.settings.ulincoreBridge
    property string formFeedback: ""

        property bool busy: false
        property bool writable: true
        property string notice: cfg.status
        property string status: cfg.status
        property var candidates: []
        property var failures: []

        property var providers: {
            const out = []
            const rows = cfg.providers
            for (let i = 0; i < rows.length; ++i) {
                const p = rows[i]
                out.push({
                    provider: p.id,
                    label: p.name,
                    displayName: p.name,
                    settingsNs: "ulincore",
                    settingsPath: [],
                    active: true,
                    declared: true,
                    noApiKey: false,
                    keyKnown: true,
                    keyConfigured: p.apiKey.length > 0,
                    keyWritable: true
                })
            }
            return out
        }

        property var models: {
            const out = []
            const rows = cfg.providers
            for (let i = 0; i < rows.length; ++i) {
                const p = rows[i]
                for (let j = 0; j < p.models.length; ++j) {
                    const m = p.models[j]
                    out.push({
                        provider: p.id,
                        providerName: p.name,
                        id: m.id,
                        name: m.name && m.name.length > 0 ? m.name : m.id,
                        description: "",
                        hasContextWindow: false,
                        contextWindow: 0,
                        hasMaxTokens: true,
                        maxTokens: m.maxTokens,
                        temperature: m.temperature,
                        declared: true,
                        userDeclared: true,
                        settingsWritable: true,
                        reasoningDisabled: true
                    })
                }
            }
            return out
        }

        property int groupCount: providers.length

        function providerById(id) {
            for (let i = 0; i < cfg.providers.length; ++i) {
                if (cfg.providers[i].id === id)
                    return cfg.providers[i]
            }
            return null
        }

        function submitModelForm(provider, id, name, contextWindow, maxTokens, efforts, apiKey) {
            if (apiKey && apiKey.length > 0)
                saveApiKey(provider, apiKey)
            const tokens = parseInt(maxTokens)
            cfg.addModel(provider, id, name,
                Number.isNaN(tokens) ? 4096 : tokens, 0.2)
            formFeedback = "已添加模型 " + id
        }

        function saveApiKey(provider, apiKey) {
            const p = providerById(provider)
            if (!p)
                return
            cfg.updateProvider(p.id, p.name, p.baseUrl, apiKey)
            formFeedback = "API Key 已保存"
        }

        function removeModel(provider, id) {
            cfg.removeModel(provider, id)
        }

        function discover(provider, apiKey) {
            if (apiKey && apiKey.length > 0)
                saveApiKey(provider, apiKey)
            if (provider && provider.length > 0)
                cfg.fetchModels(provider)
            formFeedback = "正在获取模型…"
        }

        function createCustomProvider(route, displayName, baseUrl, api, modelId, modelName, apiKey) {
            cfg.addProvider(displayName.length > 0 ? displayName : route, baseUrl, apiKey)
            const rows = cfg.providers
            if (rows.length > 0 && modelId.length > 0)
                cfg.addModel(rows[rows.length - 1].id, modelId, modelName, 4096, 0.2)
            formFeedback = "已添加提供商 " + (displayName.length > 0 ? displayName : route)
        }

        function supportsThinkingLevels() { return false }
        function routeHint() { return "写入 ULinCore 配置" }
    
}
