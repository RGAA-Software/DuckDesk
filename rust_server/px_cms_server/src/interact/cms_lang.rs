#[allow(dead_code)]
pub struct CmsLanguage {
    zh_cn: bool,
    // app settings
    pub app_name: String,
    pub copy: String,
    pub update: String,
    pub restart: String,
    pub open: String,
    pub copy_success: String,
    pub copy_failed: String,
    pub operate_success: String,
    pub refresh: String,
    pub exit: String,
    pub sure: String,
    pub cancel: String,

    // server settings
    pub server_settings: String,
    pub st_basic_info: String,
    pub st_server_id: String,
    pub st_auth_state: String,
    pub st_update_auth: String,
    pub st_cms_state: String,
    pub st_relay_state: String,
    pub st_cms_website: String,
    pub st_cms_website_https: String,
    pub st_exit_server: String,
    pub st_ask_exit_server: String,

    // server state
    pub server_state: String,
    pub redis_state: String,
    pub mongodb_state: String,
}

impl CmsLanguage {
    pub fn new_english() -> CmsLanguage {
        CmsLanguage {
            zh_cn: false,
            app_name: "GoDesk CM".to_string(),
            copy: "Copy".to_string(),
            update: "Update".to_string(),
            restart: "Restart".to_string(),
            open: "Open".to_string(),
            copy_success: "Copy Success".to_string(),
            copy_failed: "Copy Failed".to_string(),
            operate_success: "Operate Success".to_string(),
            refresh: "Refresh".to_string(),
            exit: "Exit".to_string(),
            sure: "Sure".to_string(),
            cancel: "Cancel".to_string(),
            server_settings: "Server Settings".to_string(),
            st_basic_info: "Basic Information".to_string(),
            st_server_id: "Machine Code".to_string(),
            st_auth_state: "Authorization State".to_string(),
            st_update_auth: "Update Authorization".to_string(),
            st_cms_state: "Manager Server State".to_string(),
            st_relay_state: "Relay Server State".to_string(),
            st_cms_website: "Manager Website".to_string(),
            st_cms_website_https: "Manager Website(Https)".to_string(),
            st_exit_server: "Exit Server".to_string(),
            st_ask_exit_server: "Do you want to exit the server?".to_string(),
            server_state: "Server State".to_string(),
            redis_state: "Redis State".to_string(),
            mongodb_state: "MongoDB State".to_string(),
        }
    }

    pub fn new_chinese() -> CmsLanguage {
        CmsLanguage {
            zh_cn: true,
            app_name: "GoDesk管理端".to_string(),
            copy: "复制".to_string(),
            update: "更新".to_string(),
            restart: "重启".to_string(),
            open: "打开".to_string(),

            copy_success: "复制成功".to_string(),
            copy_failed: "复制失败".to_string(),
            operate_success: "操作成功".to_string(),
            refresh: "刷新".to_string(),
            exit: "退出".to_string(),
            sure: "确定".to_string(),
            cancel: "退出".to_string(),
            server_settings: "服务设置".to_string(),
            st_basic_info: "基础信息".to_string(),
            st_server_id: "机器码".to_string(),
            st_auth_state: "授权状态".to_string(),
            st_update_auth: "更新授权".to_string(),
            st_cms_state: "管理服务状态".to_string(),
            st_relay_state: "中转服务状态".to_string(),
            st_cms_website: "管理端网页".to_string(),
            st_cms_website_https: "管理端网页(Https)".to_string(),
            st_exit_server: "退出服务".to_string(),
            st_ask_exit_server: "您确定要退出服务吗?".to_string(),
            server_state: "服务状态".to_string(),
            redis_state: "Redis状态".to_string(),
            mongodb_state: "MongoDB状态".to_string(),
        }
    }

    pub fn is_zh_cn(&self) -> bool {
        self.zh_cn
    }
}
