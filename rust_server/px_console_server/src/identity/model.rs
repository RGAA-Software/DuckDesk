use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Deserialize)]
#[serde(default)]
pub struct ResourcePageQuery {
    pub page: usize,
    pub page_size: usize,
    pub keyword: String,
    pub state: String,
}

impl Default for ResourcePageQuery {
    fn default() -> Self {
        Self {
            page: 1,
            page_size: 20,
            keyword: String::new(),
            state: String::new(),
        }
    }
}

impl ResourcePageQuery {
    pub fn normalized(&self) -> (usize, usize) {
        (self.page.clamp(1, 100_000), self.page_size.clamp(1, 100))
    }
}

#[derive(Debug, Clone, Default, Serialize)]
pub struct ResourcePage<T> {
    pub items: Vec<T>,
    pub page: usize,
    pub page_size: usize,
    pub total: usize,
}

pub fn page_items<T>(items: Vec<T>, query: &ResourcePageQuery) -> ResourcePage<T> {
    let (page, page_size) = query.normalized();
    let total = items.len();
    let start = (page - 1).saturating_mul(page_size).min(total);
    let end = start.saturating_add(page_size).min(total);
    ResourcePage {
        items: items.into_iter().skip(start).take(end - start).collect(),
        page,
        page_size,
        total,
    }
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct UserGroup {
    pub gid: String,
    pub name: String,
    pub name_normalized: String,
    pub remark: String,
    pub deleted: bool,
    pub created_at: i64,
    pub updated_at: i64,
    pub version: i64,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct UserGroupMember {
    pub uid: String,
    pub gid: String,
    pub created_at: i64,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct GroupDeviceGrant {
    pub gid: String,
    pub device_id: String,
    pub created_at: i64,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct GroupAppGrant {
    pub gid: String,
    pub app_id: String,
    pub created_at: i64,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq, Eq)]
pub struct GroupRef {
    pub gid: String,
    pub name: String,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct GroupView {
    pub gid: String,
    pub name: String,
    pub remark: String,
    pub member_count: u64,
    pub device_count: u64,
    pub app_count: u64,
    pub created_at: i64,
    pub updated_at: i64,
    pub version: i64,
}

#[cfg(test)]
mod tests {
    use super::{page_items, ResourcePageQuery};

    #[test]
    fn resource_paging_is_one_based_bounded_and_reports_total() {
        let query = ResourcePageQuery {
            page: 2,
            page_size: 2,
            ..Default::default()
        };
        let result = page_items(vec![1, 2, 3, 4, 5], &query);
        assert_eq!(result.items, vec![3, 4]);
        assert_eq!(result.page, 2);
        assert_eq!(result.page_size, 2);
        assert_eq!(result.total, 5);

        let bounded = ResourcePageQuery {
            page: 0,
            page_size: 1_000,
            ..Default::default()
        };
        let result = page_items(vec![1, 2], &bounded);
        assert_eq!(result.page, 1);
        assert_eq!(result.page_size, 100);
    }
}
