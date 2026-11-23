// DSH ModelsPage 的数据适配层：页面结构保持 DSH 原样，只把数据源换成 ULinCore。
import QtQml
import ULinHub 1.0

QtObject {
    id: adapter

    property var cfg: Hub.settings.ulincoreBridge
    property string formFeedback: ""

        // 对齐 DSH ModelListPage 读取的状态字段（busy/writable/notice/status 等），页面结构不用改
        property bool busy: false
        property bool writable: true
        property string notice: cfg.status
        property string status: cfg.status
        property var candidates: []
        property var failures: []

        // 提供商列表：字段名对齐 DSH ModelListPage 的约定，ulincore 下都算已声明且可写
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

        // 模型列表：把各提供商的 models 摊平，字段同样对齐 DSH 页面的约定
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

        // 表单签名沿用 DSH 页面；contextWindow / efforts 目前未写入内核配置。
        function submitModelForm(provider, id, name, contextWindow, maxTokens, efforts, apiKey) {
            if (apiKey && apiKey.length > 0)
                saveApiKey(provider, apiKey)
            const tokens = parseInt(maxTokens)
            cfg.addModel(provider, id, name, Number.isNaN(tokens) ? 4096 : tokens, 0.2)
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
