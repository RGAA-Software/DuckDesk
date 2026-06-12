<template>
  <div class="policy-page">
    <el-card class="policy-card" shadow="never">
      <h1>GoDesk 隐私政策</h1>
      <p class="update-time">最后更新日期：2025-12-01</p>

      <el-divider />

      <section>
        <h2>一、引言</h2>
        <p>
          GoDesk 非常重视用户的隐私与个人信息保护。
          本《隐私政策》旨在向您说明我们在您使用服务过程中，
          如何收集、使用、存储和保护您的信息。
        </p>
      </section>

      <section>
        <h2>二、信息收集范围</h2>
        <p>在您使用 GoDesk 服务时，我们可能收集以下信息：</p>
        <ul>
          <li>账号信息（如账号标识、联系方式）</li>
          <li>设备信息（设备类型、操作系统、客户端版本）</li>
          <li>连接信息（连接时间、连接状态、错误日志）</li>
          <li>日志信息（用于安全审计与问题排查）</li>
        </ul>
        <p>
          我们不会主动收集远程会话中的具体操作内容。
        </p>
      </section>

      <section>
        <h2>三、信息使用目的</h2>
        <ul>
          <li>为您提供远程连接与协助服务</li>
          <li>保障产品安全、稳定运行</li>
          <li>改进功能、优化用户体验</li>
          <li>履行法律法规规定的义务</li>
        </ul>
      </section>

      <section>
        <h2>四、信息存储与保护</h2>
        <p>
          我们采取合理的技术与管理措施，防止信息被未经授权访问、
          泄露、篡改或丢失。
        </p>
        <p>
          信息仅在实现本政策所述目的所需的最短期限内保存。
        </p>
      </section>

      <section>
        <h2>五、信息共享与披露</h2>
        <p>
          我们不会向任何第三方出售您的个人信息。
          仅在以下情形下，可能依法披露相关信息：
        </p>
        <ul>
          <li>法律法规明确要求</li>
          <li>司法或行政机关依法提出要求</li>
          <li>获得用户明确授权</li>
        </ul>
      </section>

      <section>
        <h2>六、用户权利</h2>
        <p>您有权：</p>
        <ul>
          <li>查询、访问您的个人信息</li>
          <li>更正或补充不准确的信息</li>
          <li>请求删除不再必要的信息</li>
          <li>注销账号并停止使用服务</li>
        </ul>
      </section>

      <section>
        <h2>七、未成年人保护</h2>
        <p>
          本产品主要面向具备完全民事行为能力的用户。
          若您为未成年人，请在监护人同意与指导下使用。
        </p>
      </section>

      <section>
        <h2>八、政策更新</h2>
        <p>
          我们可能会适时修订本隐私政策。
          更新后的内容将在本页面公布并生效。
        </p>
      </section>
    </el-card>
  </div>
</template>

<script setup lang="ts"></script>

<style scoped>
.policy-page {
  max-width: 960px;
  margin: 0 auto;
  padding: 24px;
}
.policy-card h1 {
  font-size: 28px;
  margin-bottom: 8px;
}
.update-time {
  color: #999;
  font-size: 14px;
}
.policy-card h2 {
  margin-top: 28px;
}
.policy-card p,
.policy-card li {
  line-height: 1.9;
}
</style>

