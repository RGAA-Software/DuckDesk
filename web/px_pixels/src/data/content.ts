/* ============================================================
   PIXELS · 站点结构数据
   说明：文案内容统一放在 src/locales/ 语言包中（zh-CN / en-US），
   此文件只保留图标、锚点、数值等非文案结构数据。
   ============================================================ */

export const navItems = [
  { key: 'services', href: '#services' },
  { key: 'industries', href: '#industries' },
  { key: 'cooperation', href: '#cooperation' },
  { key: 'advantages', href: '#advantages' },
  { key: 'faq', href: '#faq' },
]

/* ---------- Hero 数据（labelKey 对应语言包 stats.*） ---------- */
export const stats = [
  { value: '100+', labelKey: 'companies' },
  { value: '30+', labelKey: 'nodes' },
  { value: '10+', labelKey: 'zones' },
  { value: '50P+', labelKey: 'power' },
]

/* ---------- 技术服务（4 大业务，key 对应语言包 services.items.*） ---------- */
export const services = [
  {
    key: 'cloudRender',
    icon: [
      '..xxxx..',
      '.xx..xx.',
      '.x....x.',
      '.x....x.',
      '.xx..xx.',
      '..xxxx..',
    ],
  },
  {
    key: 'cloudGame',
    /* 像素手柄（Xbox 风格：中间圆形 Xbox 键 + 两侧摇杆） */
    icon: [
      '.xx......xx.',
      'x..x....x..x',
      'x...xxxx...x',
      'x...xxxx...x',
      'x..x....x..x',
      'xxxx....xxxx',
      'x..........x',
      '.xxxxxxxxxx.',
    ],
  },
  {
    key: 'cloudDesktop',
    icon: [
      'xxxxxxxxxx',
      'x........x',
      'x........x',
      'x........x',
      'x........x',
      'xxxxxxxxxx',
      '...xxxx...',
      '.xxxxxxxx.',
    ],
  },
  {
    key: 'remoteDesktop',
    icon: [
      'x......x',
      '.x....x.',
      '..x..x..',
      '...xx...',
      '...xx...',
      '..x..x..',
      '.x....x.',
      'x......x',
    ],
  },
]

/* 每项服务的能力点数量（语言包 points 数组长度） */
export const servicePointCount = 3

/* ---------- 行业应用 ---------- */
export const industries = [
  {
    key: 'game',
    icon: [
      '..xxxx..',
      '.x....x.',
      'x..xx..x',
      'x..xx..x',
      'x......x',
      '.x....x.',
      '..xxxx..',
    ],
  },
  {
    key: 'film',
    icon: [
      'xxxxxxxx',
      'xxxxxxxx',
      '........',
      'xxxxxxxx',
      'xxxxxxxx',
      'xxxxxxxx',
      'xxxxxxxx',
    ],
  },
  {
    key: 'government',
    icon: [
      '...xx...',
      '..xxxx..',
      '..xxxx..',
      '..x..x..',
      '..x..x..',
      'xxxxxxxx',
      'xxxxxxxx',
    ],
  },
  {
    key: 'design',
    icon: [
      '...xx...',
      '..xxxx..',
      'xxxxxxxx',
      'xxxxxxxx',
      '.xxxxxx.',
      '..xxxx..',
      '...xx...',
    ],
  },
  {
    key: 'software',
    icon: [
      '.xxxxxx.',
      '.....x..',
      '....x...',
      '...x....',
      '..x.....',
      '.xxxxxx.',
    ],
  },
]

/* ---------- 合作模式（featured 卡片显示"推荐"徽章） ---------- */
export const cooperation = [
  {
    key: 'custom',
    featured: true,
    icon: [
      '.xxxxxx.',
      'x.xxxx.x',
      'xx.xx.xx',
      'xxx..xxx',
      'xxx..xxx',
      'xx.xx.xx',
      'x.xxxx.x',
      '.xxxxxx.',
    ],
  },
  {
    key: 'api',
    icon: [
      '..xx..xx..',
      '.x..x.x..x',
      '..........',
      '..........',
      '..........',
      '.x..x.x..x',
      '..xx..xx..',
    ],
  },
  {
    key: 'private',
    featured: true,
    icon: [
      'xxxxxxxx',
      'xxxxxxxx',
      '........',
      'xxxxxxxx',
      'xxxxxxxx',
      '........',
      'xxxxxxxx',
      'xxxxxxxx',
    ],
  },
  {
    key: 'joint',
    icon: [
      '.xx...xx.',
      '.xx...xx.',
      '.........',
      '.xxxxxxx.',
      '.xxxxxxx.',
      '.........',
    ],
  },
]

/* ---------- 技术优势 ---------- */
export const advantages = [
  {
    key: 'latency',
    icon: [
      '...xx...',
      '...xx...',
      '..xxxx..',
      '..xxxx..',
      '.xxxxxx.',
      '.xxxxxx.',
      'xxxxxxxx',
    ],
  },
  {
    key: 'quality',
    icon: [
      '...xx...',
      '..xxxx..',
      'xxxxxxxx',
      'xxxxxxxx',
      '.xxxxxx.',
      '..xxxx..',
      '...xx...',
    ],
  },
  {
    key: 'security',
    icon: [
      '.xxxxxx.',
      '.xxxxxx.',
      '.xxxxxx.',
      '.xxxxxx.',
      '.xxxxxx.',
      '..xxxx..',
      '...xx...',
    ],
  },
  {
    key: 'scalability',
    icon: [
      '...xx...',
      '...xx...',
      '...xx...',
      'xxxxxxxx',
      '...xx...',
      '...xx...',
      '...xx...',
    ],
  },
  {
    key: 'compatibility',
    icon: [
      '.xx.xx.xx',
      '.xx.xx.xx',
      '.........',
      '.xx.xx.xx',
      '.xx.xx.xx',
    ],
  },
  {
    key: 'support',
    icon: [
      '..xxxx..',
      '.x....x.',
      'x......x',
      'x..xx..x',
      'x......x',
      '.x....x.',
      '..xxxx..',
    ],
  },
]

/* ---------- 服务流程 ---------- */
export const processSteps = [{ key: 'inquiry' }, { key: 'design' }, { key: 'delivery' }, { key: 'support' }]

/* ---------- 常见问题 ---------- */
export const faqs = [
  { key: 'flow' },
  { key: 'deployment' },
  { key: 'security' },
  { key: 'integration' },
  { key: 'availability' },
  { key: 'cost' },
]

/* ---------- 联系方式（占位） ---------- */
export const contactInfo = {
  phone: '400-XXX-XXXX',
  email: 'contact@pixels.yun',
  address: 'XXX 省 XX 市 XX 区（占位地址）',
}
