<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="en">
<context>
    <name></name>
    <message id="chat_tool_call_fmt">
        <translation>Tool call: %1</translation>
    </message>
    <message id="chat_tool_result_label">
        <translation>Tool result</translation>
    </message>
    <message id="common_copy">
        <translation>Copy</translation>
    </message>
    <message id="ext_delete_dir_failed_fmt">
        <translation>Could not delete the extension directory: %1</translation>
    </message>
    <message id="ext_delete_resource_dir_failed_fmt">
        <translation>Could not delete the extension resource directory: %1</translation>
    </message>
    <message id="ask_confirm_questions">
        <translation>Please confirm the following</translation>
    </message>
    <message id="ask_answer_placeholder">
        <translation>Type your answer</translation>
    </message>
    <message id="ask_submit_answer">
        <translation>Submit answer</translation>
    </message>
    <message id="ask_tool_approval_title">
        <translation>Tool call requires approval</translation>
    </message>
    <message id="ask_tool_fmt">
        <translation>Tool: %1</translation>
    </message>
    <message id="ask_reason_fmt">
        <translation>Reason: %1</translation>
    </message>
    <message id="common_reject">
        <translation>Reject</translation>
    </message>
    <message id="ask_allow_once">
        <translation>Allow once</translation>
    </message>
    <message id="model_missing_provider_or_id">
        <translation>Missing provider or model id</translation>
    </message>
    <message id="model_credential_missing">
        <translation>Credential reference or value is missing</translation>
    </message>
    <message id="session_untitled">
        <translation>Untitled session</translation>
    </message>
    <message id="theme_switching">
        <translation>Switching theme...</translation>
    </message>
    <message id="toolfilter_no_session">
        <translation>No session is open yet</translation>
    </message>
    <message id="toolfilter_server_not_ready">
        <translation>The server address is not ready yet</translation>
    </message>
    <message id="toolfilter_plugin_unavailable">
        <translation>The tool-filter plugin is not installed or not enabled.</translation>
    </message>
    <message id="toolfilter_save_failed_http_fmt">
        <translation>Save failed (HTTP %1)</translation>
    </message>
    <message id="toolfilter_save_failed_reason_fmt">
        <translation>Save failed (%1)</translation>
    </message>
    <message id="workdir_create_failed_fmt">
        <translation>Could not create the workspace: %1 %2</translation>
    </message>
    <message id="session_new_failed_fmt">
        <translation>Could not create a new session: %1 %2</translation>
    </message>
    <message id="session_delete_label">
        <translation>Delete session</translation>
    </message>
    <message id="session_delete_confirm">
        <translation>Delete this session? This cannot be undone.</translation>
    </message>
    <message id="session_delete_failed_fmt">
        <translation>Could not delete the session: %1 %2</translation>
    </message>
    <message id="common_load_more">
        <translation>Load more</translation>
    </message>
    <message id="session_loading">
        <translation>Connected to the server; loading sessions...</translation>
    </message>
    <message id="chat_send_failed_fmt">
        <translation>Send failed: %1 %2</translation>
    </message>
    <message id="chat_no_model_hint">
        <translation>No model is configured yet. Add a model in Settings before sending.</translation>
    </message>
    <message id="chat_empty_reply_hint">
        <translation>No reply was produced this turn. Check the model configuration or the server logs.</translation>
    </message>
    <message id="server_auth_token_missing">
        <translation>Could not authenticate with the server. Please check that the server is running.</translation>
    </message>
    <message id="server_bundled_server_missing">
        <translation>The installation is incomplete, so the built-in server cannot start. Please reinstall.</translation>
    </message>
    <message id="com_member_not_found_fmt">
        <translation>COM member not found: %1 (hr=%2)</translation>
    </message>
    <message id="com_get_property_failed_fmt">
        <translation>Failed to read the COM property: %1 (hr=%2)</translation>
    </message>
    <message id="com_invoke_method_failed_fmt">
        <comment>运行时由 C++ 侧补上右括号，故本条不带 ")"。</comment>
        <translation>Failed to invoke the COM method: %1 (hr=%2</translation>
    </message>
    <message id="com_arg_type_mismatch_fmt">
        <comment>作为上一条的补充片段拼接在末尾，故以 ", " 开头。</comment>
        <translation>, argument %1 has a mismatched type</translation>
    </message>
    <message id="com_put_property_failed_fmt">
        <translation>Failed to write the COM property: %1 (hr=%2)</translation>
    </message>
    <message id="com_missing_config">
        <translation>This tool is missing the "Com": { "ProgId": ... } configuration</translation>
    </message>
    <message id="com_missing_member_arg">
        <translation>Missing required argument member (the COM property or method name to call)</translation>
    </message>
    <message id="com_kind_unsupported">
        <translation>kind only supports method / get / put</translation>
    </message>
    <message id="com_progid_invalid_fmt">
        <translation>ProgID is invalid or not registered: %1 (hr=%2)</translation>
    </message>
    <message id="com_create_failed_fmt">
        <translation>Failed to create the COM component: %1 (hr=%2)</translation>
    </message>
    <message id="com_property_chain_failed_fmt">
        <translation>Failed to descend the property chain (%1.%2): %3</translation>
    </message>
    <message id="com_property_not_object_fmt">
        <translation>Property %1.%2 is not an object, so it cannot be descended further</translation>
    </message>
    <message id="com_params_type_unsupported_fmt">
        <translation>params only supports string/number/bool/null (item %1 is invalid)</translation>
    </message>
    <message id="com_value_type_unsupported">
        <translation>value only supports string/number/bool/null</translation>
    </message>
    <message id="com_returned_object_summary_only">
        <translation>A COM object was returned; this version only provides a summary. To go deeper, use a member that returns a scalar</translation>
    </message>
    <message id="com_missing_description">
        <translation>The com tool is missing the "Com": { "ProgId": ... } description</translation>
    </message>
    <message id="com_function_requires_tool">
        <translation>Every Function requires 'Tool'; the default interface also requires 'Func'</translation>
    </message>
    <message id="server_stream_not_ready">
        <translation>The server is not ready yet, so this cannot be submitted. Please try again shortly.</translation>
    </message>
    <message id="chat_turn_step_fmt">
        <translation>%1 turns · %2 steps</translation>
    </message>
    <message id="chat_llm_label_fmt">
        <translation>LLM %1</translation>
    </message>
    <message id="chat_tool_call_count_fmt">
        <translation>%1 tool calls</translation>
    </message>
    <message id="chat_first_token_avg_fmt">
        <translation>First token avg %1</translation>
    </message>
    <message id="chat_tok_per_sec_fmt">
        <translation>%1 tok/s</translation>
    </message>
    <message id="chat_cache_hit_fmt">
        <translation>Cache hit %1%</translation>
    </message>
    <message id="chat_token_usage_fmt">
        <translation>Input %1 tok · Output %2 tok</translation>
    </message>
    <message id="chat_input_placeholder">
        <translation>Type a message. Enter to send, Shift+Enter for a new line</translation>
    </message>
    <message id="chat_stop_output">
        <translation>Stop generating</translation>
    </message>
    <message id="common_send">
        <translation>Send</translation>
    </message>
    <message id="ext_manager_title">
        <translation>Extension Manager</translation>
    </message>
    <message id="ext_install">
        <translation>Install extension</translation>
    </message>
    <message id="ext_remove_selected">
        <translation>Remove selected</translation>
    </message>
    <message id="common_refresh">
        <translation>Refresh</translation>
    </message>
    <message id="ext_empty_hint">
        <translation>No extensions installed. Click "Install extension" to pick an .ext file.</translation>
    </message>
    <message id="ext_kind_client">
        <translation>Client extension</translation>
    </message>
    <message id="ext_client_not_loaded">
        <translation>not loaded</translation>
    </message>
    <message id="ext_installed_count_fmt">
        <translation>%1 extension(s) installed.</translation>
    </message>
    <message id="ext_choose_file_title">
        <translation>Choose a DSH extension</translation>
    </message>
    <message id="ext_installing">
        <translation>Installing extension...</translation>
    </message>
    <message id="ext_install_failed_fmt">
        <translation>Extension installation failed: %1</translation>
    </message>
    <message id="ext_install_success_fmt">
        <translation>Extension installed: %1</translation>
    </message>
    <message id="ext_remove_confirm_title">
        <translation>Confirm removal</translation>
    </message>
    <message id="ext_remove_confirm_fmt">
        <translation>Remove the extension "%1"?</translation>
    </message>
    <message id="ext_removed_fmt">
        <translation>Extension removed: %1</translation>
    </message>
    <message id="ext_partial_delete_failed_fmt">
        <translation>Extension configuration cleared, but some files could not be deleted: %1</translation>
    </message>
    <message id="ext_patch_update_failed_fmt">
        <translation>The extension was removed, but its configuration could not be updated: %1</translation>
    </message>
    <message id="ext_patch_read_failed">
        <translation>Could not read the extension configuration, so cleanup was skipped.</translation>
    </message>
    <message id="ext_no_residue">
        <translation>No leftover extension configuration found</translation>
    </message>
    <message id="ext_residue_cleaned_fmt">
        <translation>Cleaned up leftover extension: %1</translation>
    </message>
    <message id="ext_build_config_mismatch_hint">
        <comment>作为 QPluginLoader::errorString() 的补充片段追加在末尾，前置分隔符 " — " 必须保留；本身是完整句，不是拼接残片。</comment>
        <translation> — This plugin does not match the current version. Please rebuild it or use a compatible build.</translation>
    </message>
    <message id="ext_copy_file_failed_fmt">
        <comment>原文是英中混排（英文前缀 + 中文提示），key 化时合并成一条，否则译文里只剩半句。</comment>
        <translation>Could not copy the client extension file: %1 (if the extension is running, its DLL is held by this process; restart the client and install again)</translation>
    </message>
    <message id="model_source_custom">
        <translation>Customized</translation>
    </message>
    <message id="model_source_adapter_builtin">
        <translation>Adapter built-in</translation>
    </message>
    <message id="model_provider_route">
        <translation>Provider route</translation>
    </message>
    <message id="model_id_label">
        <translation>Model ID</translation>
    </message>
    <message id="model_display_name">
        <translation>Display name</translation>
    </message>
    <message id="model_description_label">
        <translation>Description</translation>
    </message>
    <message id="model_context_window">
        <translation>Context window</translation>
    </message>
    <message id="model_max_output">
        <translation>Max output</translation>
    </message>
    <message id="model_think_level">
        <translation>Thinking level</translation>
    </message>
    <message id="model_think_not_offered">
        <translation>Declared as not offered</translation>
    </message>
    <message id="model_think_undeclared_fmt">
        <translation>This model does not publish it (the UI offers generic levels: %1)</translation>
    </message>
    <message id="model_config_source">
        <translation>Config source</translation>
    </message>
    <message id="model_source_settings_user">
        <translation>Added in settings</translation>
    </message>
    <message id="model_source_settings_bundled">
        <translation>Shipped with the app</translation>
    </message>
    <message id="model_source_adapter_only">
        <translation>Provided by the server</translation>
    </message>
    <message id="model_settings_readonly_suffix">
        <translation>(settings read-only)</translation>
    </message>
    <message id="model_writeback_target">
        <translation>Saved to</translation>
    </message>
    <message id="common_delete">
        <translation>Delete</translation>
    </message>
    <message id="model_list_title">
        <translation>Model list</translation>
    </message>
    <message id="model_list_desc">
        <translation>The model catalog is published by the server adapter. Models you add are written to the server's settings document and take effect immediately for both new and current sessions. Click any entry to expand its details.</translation>
    </message>
    <message id="model_add">
        <translation>Add model</translation>
    </message>
    <message id="model_server_not_ready">
        <translation>The server is not ready; the model catalog cannot be read yet.</translation>
    </message>
    <message id="model_loading_catalog">
        <translation>Reading the server model catalog...</translation>
    </message>
    <message id="model_catalog_empty">
        <translation>The server published no models. Use "Add model" below to write one into settings.</translation>
    </message>
    <message id="model_catalog_summary_fmt">
        <translation>%1 models across %2 providers.</translation>
    </message>
    <message id="model_add_readonly">
        <translation>Server settings are read-only; models cannot be added.</translation>
    </message>
    <message id="model_add_requires_api">
        <translation>Adding models is not supported on the current connection (the model configuration is read-only).</translation>
    </message>
    <message id="model_add_title">
        <translation>Add model</translation>
    </message>
    <message id="model_id_hint">
        <translation>Required; the model string used by the server protocol</translation>
    </message>
    <message id="model_fetch_models">
        <translation>Fetch models</translation>
    </message>
    <message id="model_fetch_hint">
        <translation>Ask the server which models this route advertises, then pick one from the dropdown</translation>
    </message>
    <message id="model_fetch_failed_fmt">
        <translation>Could not fetch models: %1 %2</translation>
    </message>
    <message id="model_discovery_missing_namespace">
        <translation>No provider namespace, so models cannot be fetched</translation>
    </message>
    <message id="model_display_name_hint">
        <translation>Optional; the name shown in the UI (defaults to the model ID)</translation>
    </message>
    <message id="model_context_window_hint">
        <translation>Optional, e.g. 1000000</translation>
    </message>
    <message id="model_max_output_hint">
        <translation>Optional, e.g. 384000</translation>
    </message>
    <message id="model_think_level_hint">
        <translation>Optional, comma-separated: off/low/high/max; leave empty to let the adapter decide</translation>
    </message>
    <message id="model_input_modality">
        <translation>Input modality</translation>
    </message>
    <message id="model_supports_image">
        <translation>Supports image input</translation>
    </message>
    <message id="model_credential_ref_hint">
        <translation>Optional; if filled in, written as this route's credential reference</translation>
    </message>
    <message id="common_cancel">
        <translation>Cancel</translation>
    </message>
    <message id="common_add">
        <translation>Add</translation>
    </message>
    <message id="model_delete_readonly">
        <translation>Server settings are read-only; models cannot be deleted.</translation>
    </message>
    <message id="model_deleted_fmt">
        <translation>Deleted model %1.</translation>
    </message>
    <message id="model_delete_failed_fmt">
        <translation>Could not delete model %1: %2 %3</translation>
    </message>
    <message id="model_namespace_missing_fmt">
        <translation>Could not read the configuration for %1, so this cannot be saved right now.</translation>
    </message>
    <message id="model_write_progress_fmt">
        <translation>Writing %1.%2 (%3 entries so far).</translation>
    </message>
    <message id="model_write_warning_desc">
        <translation>Note: this route currently uses the adapter's bundled catalog. After writing, the route will publish only this list, and the remaining models will disappear from the model list.</translation>
    </message>
    <message id="model_write_deepseek_warning">
        <translation>Note: this write will replace the provider's entire model list.</translation>
    </message>
    <message id="model_write_readonly">
        <translation>Server settings are read-only; the write will be rejected.</translation>
    </message>
    <message id="model_provider_route_suffix_fmt">
        <translation>(%1)</translation>
    </message>
    <message id="model_disabled_suffix">
        <translation> · disabled</translation>
    </message>
    <message id="model_no_configurable_provider">
        <translation>The server offers no configurable provider route, so models cannot be added.</translation>
    </message>
    <message id="model_credential_derived_fmt">
        <translation>Credential %1 (auto-derived)</translation>
    </message>
    <message id="model_credential_from_profile_fmt">
        <translation>Credential %1 (specified by the server configuration)</translation>
    </message>
    <message id="model_credential_unknown_suffix">
        <translation>; status unknown, you can fill it in directly (the server validates on write)</translation>
    </message>
    <message id="model_credential_readonly_suffix">
        <translation>; marked read-only by the server (managed by the deployment; set it in the environment)</translation>
    </message>
    <message id="model_credential_configured_fmt">
        <translation>; already configured on the server%1</translation>
    </message>
    <message id="model_credential_source_fmt">
        <translation>(source: %1)</translation>
    </message>
    <message id="model_credential_unconfigured_suffix">
        <translation>; not configured on the server yet</translation>
    </message>
    <message id="model_credential_readonly_hint">
        <translation>This credential is read-only, just leave it empty</translation>
    </message>
    <message id="model_select_provider_first">
        <translation>Select a provider route first.</translation>
    </message>
    <message id="model_id_required">
        <translation>Model ID cannot be empty.</translation>
    </message>
    <message id="model_numeric_invalid">
        <translation>Context window and max output must be positive integers, or left empty.</translation>
    </message>
    <message id="model_think_level_unknown_fmt">
        <translation>Unknown thinking level: %1 (available: %2)</translation>
    </message>
    <message id="model_writing">
        <translation>Writing to the server...</translation>
    </message>
    <message id="model_writing_credential">
        <translation>Writing credential and model...</translation>
    </message>
    <message id="model_credential_write_failed_fmt">
        <translation>Could not write credential %1: %2 %3 (no model was added)</translation>
    </message>
    <message id="model_updated_fmt">
        <translation>Updated model %1 (overwrote the entry with the same ID).</translation>
    </message>
    <message id="model_added_fmt">
        <translation>Added model %1.</translation>
    </message>
    <message id="model_add_failed_fmt">
        <translation>Add failed: %1 %2</translation>
    </message>
    <message id="model_label">
        <translation>Model</translation>
    </message>
    <message id="model_think_depth">
        <translation>Thinking depth</translation>
    </message>
    <message id="model_none_published">
        <translation>The server published no models</translation>
    </message>
    <message id="model_think_generic_note">
        <translation>This model does not publish thinking levels; the generic levels are shown below.</translation>
    </message>
    <message id="plugin_market_title">
        <translation>Plugin marketplace</translation>
    </message>
    <message id="plugin_search_placeholder">
        <translation>Search plugins...</translation>
    </message>
    <message id="plugin_restart_server">
        <translation>Restart server</translation>
    </message>
    <message id="common_prev_page">
        <translation>Previous</translation>
    </message>
    <message id="common_next_page">
        <translation>Next</translation>
    </message>
    <message id="plugin_installed">
        <translation>Installed</translation>
    </message>
    <message id="plugin_loading">
        <translation>Loading the plugin marketplace...</translation>
    </message>
    <message id="plugin_loaded_offline">
        <translation>Plugin marketplace loaded (offline snapshot); waiting for fresh data...</translation>
    </message>
    <message id="plugin_loaded_cached_fmt">
        <translation>Plugin marketplace loaded (cache), %1 plugins</translation>
    </message>
    <message id="plugin_loaded_fmt">
        <translation>Plugin marketplace loaded, %1 plugins</translation>
    </message>
    <message id="plugin_load_failed_fmt">
        <translation>Could not load the plugin marketplace: %1</translation>
    </message>
    <message id="plugin_install_failed">
        <translation>Plugin marketplace installation failed; see the output below</translation>
    </message>
    <message id="plugin_install_done_restarting">
        <translation>Plugin marketplace installed; restarting the server...</translation>
    </message>
    <message id="plugin_action_submitted">
        <translation>Action submitted, refreshing...</translation>
    </message>
    <message id="plugin_action_failed_fmt">
        <translation>Action failed: %1</translation>
    </message>
    <message id="plugin_page_fmt">
        <translation>Page %1 / %2</translation>
    </message>
    <message id="plugin_install_action">
        <translation>Install</translation>
    </message>
    <message id="plugin_update_action">
        <translation>Update</translation>
    </message>
    <message id="plugin_uninstall_action">
        <translation>Uninstall</translation>
    </message>
    <message id="common_close">
        <translation>Close</translation>
    </message>
    <message id="settings_title">
        <translation>Settings</translation>
    </message>
    <message id="settings_agent_preset_tab">
        <translation>Agent preset</translation>
    </message>
    <message id="settings_server_tab">
        <translation>Server</translation>
    </message>
    <message id="settings_appearance_tab">
        <translation>Appearance</translation>
    </message>
    <message id="settings_default_agent_preset">
        <translation>Default agent preset</translation>
    </message>
    <message id="settings_agent_preset_hint">
        <translation>New sessions use this preset; changes also take effect immediately in the current session.</translation>
    </message>
    <message id="settings_server_address">
        <translation>Server address</translation>
    </message>
    <message id="settings_server_address_hint">
        <translation>Leave empty to use the built-in DSH server; restart the server for changes to take effect.</translation>
    </message>
    <message id="settings_save_and_restart">
        <translation>Save and restart server</translation>
    </message>
    <message id="settings_appearance_desc">
        <translation>The interface comes from built-in resources (released into the styles/ folder on first launch so you can customise them). Click the button below to restore the built-in versions.</translation>
    </message>
    <message id="settings_reset_styles">
        <translation>Reset QML to default</translation>
    </message>
    <message id="settings_styles_reset_done">
        <translation>Default appearance restored. The interface has been refreshed.</translation>
    </message>
    <message id="settings_language_label">
        <translation>Interface language:</translation>
    </message>
    <message id="settings_language_follow_system">
        <translation>Follow system</translation>
    </message>
    <message id="settings_language_switched">
        <translation>Interface language changed.</translation>
    </message>
    <message id="settings_language_pack_missing_fmt">
        <translation>No language pack found for %1; reverted to the default language. Put ulinhub_%1.qm into the program's translations/ directory to enable it.</translation>
    </message>
    <message id="settings_language_system_name">
        <translation>System language</translation>
    </message>
    <message id="common_loading">
        <translation>Loading...</translation>
    </message>
    <message id="common_load_failed_fmt">
        <translation>Load failed: %1 %2</translation>
    </message>
    <message id="settings_no_preset">
        <translation>(no preset available)</translation>
    </message>
    <message id="sidebar_new_workspace">
        <translation>New workspace</translation>
    </message>
    <message id="sidebar_clear_sessions">
        <translation>Clear sessions</translation>
    </message>
    <message id="sidebar_plugins">
        <translation>Plugins</translation>
    </message>
    <message id="sidebar_switch_theme">
        <translation>Switch theme</translation>
    </message>
    <message id="sidebar_ungrouped">
        <translation>Ungrouped</translation>
    </message>
    <message id="toolfilter_group_toggle_tip">
        <translation>When checked, this group is collapsed (its tools are hidden). Expand it again whenever you need them.</translation>
    </message>
    <message id="toolfilter_params_label">
        <translation>Parameters:</translation>
    </message>
    <message id="toolfilter_no_tools">
        <translation>No tools</translation>
    </message>
    <message id="toolfilter_summary_fmt">
        <translation>%1 tools · %2 hidden</translation>
    </message>
    <message id="toolfilter_group_hidden_suffix">
        <translation> · whole group hidden in config</translation>
    </message>
    <message id="toolfilter_title">
        <translation>Tool filter</translation>
    </message>
    <message id="toolfilter_show_all">
        <translation>Show all</translation>
    </message>
    <message id="toolfilter_hide_all">
        <translation>Hide all</translation>
    </message>
    <message id="toolfilter_help_desc">
        <translation>Uncheck to hide a tool; it can still be called in the conversation when needed.</translation>
    </message>
    <message id="toolfilter_loading">
        <translation>Reading this session's tool catalog...</translation>
    </message>
    <message id="toolfilter_no_filterable_tools">
        <translation>This session has no filterable tools</translation>
    </message>
    <message id="toolfilter_saving">
        <translation>Saving...</translation>
    </message>
    <message id="toolfilter_summary_full_fmt">
        <translation>%1 tools in %2 directories, %3 hidden (hiding only affects the prompt)</translation>
    </message>
    <message id="toolfilter_global_layer_only_suffix">
        <translation> · the server returned only the global layer, so the catalog may be incomplete</translation>
    </message>
    <message id="placeholder_session_current">
        <translation>Current session</translation>
    </message>
    <message id="placeholder_session_none">
        <translation>No session selected</translation>
    </message>
    <message id="placeholder_session_hint">
        <translation>Pick one from the list on the left, or click ＋ to create one</translation>
    </message>
    <message id="right_sidebar_title">
        <translation>Panel</translation>
    </message>
    <message id="right_sidebar_empty_hint">
        <translation>Nothing here yet.
Terminal, files and preview will show up in this panel.</translation>
    </message>
    <message id="sidebar_no_sessions">
        <translation>No sessions yet
Click ＋ at the top right to create one</translation>
    </message>
    <message id="workdir_delete_label">
        <translation>Delete workspace</translation>
    </message>
    <message id="workdir_delete_confirm">
        <translation>Delete this workspace? It is only removed from the workspace list; its sessions are kept.</translation>
    </message>
    <message id="chat_thinking_title">
        <translation>Thinking</translation>
    </message>
    <message id="chat_streaming_meta">
        <translation>In progress...</translation>
    </message>
    <message id="workdir_new_session_tip">
        <translation>New session in this workspace</translation>
    </message>
    <message id="common_clear">
        <translation>Clear</translation>
    </message>
    <message id="topbar_right_sidebar_tip">
        <translation>Right sidebar</translation>
    </message>
    <message id="chat_session_busy_restarting">
        <translation>The previous process has not exited yet. Restarting the service and retrying…</translation>
    </message>
    <message id="chat_session_busy_status">
        <translation>Restarting the service…</translation>
    </message>
    <message id="chat_command_failed_fmt">
        <translation>Command failed: %1</translation>
    </message>
    <message id="server_starting">
        <translation>Starting the built-in server...</translation>
    </message>
    <message id="session_list_load_failed_fmt">
        <translation>Could not load the session list: %1</translation>
    </message>
    <message id="workdir_delete_failed_fmt">
        <translation>Could not delete the workspace: %1 %2</translation>
    </message>
    <message id="ext_abi_mismatch_hint">
        <translation>This extension is not compatible with the current version and was only partially loaded. Please update the extension or the app.</translation>
    </message>
    <message id="ext_installed_restart_hint">
        <translation>The tool extension is installed — it loads after the built-in server restarts</translation>
    </message>
    <message id="ext_removed_client_hint">
        <translation>The client extension was removed — restart ULin Hub to finish uninstalling it</translation>
    </message>
    <message id="ext_removed_tool_hint">
        <translation>The tool extension was removed — restart the built-in server for it to take effect</translation>
    </message>
    <message id="ext_residue_cleaned_restart_hint">
        <translation>Leftover entries cleaned up — restart the built-in server for it to take effect</translation>
    </message>
    <message id="plugin_installing_market">
        <translation>Preparing the plugin marketplace, please wait…</translation>
    </message>
    <message id="session_none_click_new">
        <translation>No session yet — click + at the top left to create one.</translation>
    </message>
    <message id="server_not_ready_yet">
        <translation>The server is not ready yet</translation>
    </message>
    <message id="settings_provider_namespace_missing_fmt">
        <translation>Could not find the configuration for %1. Please check the server settings.</translation>
    </message>
    <message id="model_provider_none_reported">
        <translation>The server reported no configurable model provider.</translation>
    </message>
    <message id="settings_read_failed_fmt">
        <translation>Could not read settings: %1 %2</translation>
    </message>
    <message id="model_api_key_required">
        <translation>Enter an API key first.</translation>
    </message>
    <message id="model_credential_written_fmt">
        <translation>Credential %1 written. You can now click "Fetch models" to list the available models.</translation>
    </message>
    <message id="model_api_key_write_failed_fmt">
        <translation>Could not write the credential: %1 %2</translation>
    </message>
    <message id="model_fetch_none_found">
        <translation>No model found (is the API key correct?)</translation>
    </message>
    <message id="model_fetch_found_fmt">
        <translation>Found %1 candidate model(s); click "Add" to write one into the config.</translation>
    </message>
    <message id="style_pack_unreadable">
        <translation>The style pack configuration could not be read. Reverted to the default appearance.</translation>
    </message>
    <message id="style_pack_invalid_json">
        <translation>The style pack configuration is malformed. Reverted to the default appearance.</translation>
    </message>
    <message id="command_unknown_fmt">
        <translation>Unknown command: /%1</translation>
    </message>
    <message id="ext_delete_locked">
        <translation>Some files were still in use and could not be deleted. The extension is unloaded; leftovers are cleaned up on the next launch.</translation>
    </message>
    <message id="style_pack_abi_mismatch">
        <translation>This style pack is not compatible with the current version. Reverted to the default appearance.</translation>
    </message>
    <message id="server_start_failed">
        <translation>The built-in server failed to start. Please restart the app; if it keeps happening, reinstall.</translation>
    </message>
    <message id="server_proc_start_failed">
        <translation>Could not start the built-in server. Please restart the app.</translation>
    </message>
    <message id="server_auth_failed">
        <translation>Could not authenticate with the server. Please restart the app and try again.</translation>
    </message>
    <message id="server_auth_queue_full">
        <translation>The server is not ready yet. Please try again in a moment.</translation>
    </message>
    <message id="toolfilter_plugin_no_response">
        <translation>The tool-filter plugin did not respond. Please try again shortly.</translation>
    </message>
    <message id="model_provider_load_failed_fmt">
        <translation>Could not load the model list for %1: %2</translation>
    </message>
</context>
</TS>
