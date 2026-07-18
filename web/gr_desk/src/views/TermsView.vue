<script setup lang="ts">
import { useI18n } from 'vue-i18n'
import { useRouter } from 'vue-router'

const { t } = useI18n()
const router = useRouter()
</script>

<template>
  <div class="policy-page">
    <div class="policy-card cyber-panel">
      <el-link type="primary" :underline="false" class="back-link" @click="router.push('/main')">
        ← {{ t('legal.backHome') }}
      </el-link>
      <h1>{{ t('legal.termsTitle') }}</h1>
      <p class="update-time">最后更新日期：2025-12-01</p>

      <el-divider />

      <section>
        <h2>一、总则</h2>
        <p>
          欢迎您使用 GoDesk（以下简称“本产品”或“我们”）。
          GoDesk 是一款提供远程办公、远程协助、设备访问与管理能力的软件工具。
        </p>
        <p>
          本《使用条款》（以下简称“本条款”）是您与 GoDesk 之间就使用相关服务所订立的法律协议。
          在您注册、登录、下载、安装或以任何形式使用本产品之前，请务必仔细阅读并充分理解本条款内容。
        </p>
        <p>
          一旦您开始使用本产品，即视为您已阅读、理解并同意接受本条款的全部约定。
        </p>
      </section>

      <section>
        <h2>二、服务内容</h2>
        <p>GoDesk 向用户提供的服务包括但不限于：</p>
        <ul>
          <li>远程桌面连接与实时控制</li>
          <li>远程协助与技术支持功能</li>
          <li>文件传输、剪贴板共享</li>
          <li>设备接入、会话与权限管理</li>
          <li>与远程连接相关的附加功能</li>
        </ul>
        <p>
          我们有权根据产品规划、技术条件或法律法规要求，对服务内容进行新增、调整或中止。
        </p>
      </section>

      <section>
        <h2>三、账号注册与管理</h2>
        <p>
          用户在使用部分功能前，可能需要注册 GoDesk 账号。
          用户应提供真实、准确、完整的信息，并及时更新。
        </p>
        <p>
          用户应妥善保管账号及凭证，并对使用该账号进行的所有活动承担法律责任。
        </p>
        <p>
          若发现账号存在被盗用或异常使用的情况，用户应立即通知我们。
        </p>
      </section>

      <section>
        <h2>四、用户行为规范</h2>
        <p>用户在使用 GoDesk 服务时，应遵守法律法规及社会公序良俗，不得从事以下行为：</p>
        <ul>
          <li>未经授权访问、控制或干扰他人设备</li>
          <li>从事任何违法、侵权或侵犯他人合法权益的行为</li>
          <li>传播恶意程序、病毒、木马或其他有害内容</li>
          <li>规避、破坏或干扰本产品的安全机制</li>
          <li>将本产品用于非法监控、数据窃取等用途</li>
        </ul>
        <p>
          如用户违反上述规定，我们有权限制、暂停或终止其使用服务，并依法追究责任。
        </p>
      </section>

      <section>
        <h2>五、远程连接与数据责任</h2>
        <p>
          GoDesk 仅作为远程连接与协助的技术工具，
          远程会话中的操作行为、内容传输及其后果由用户自行负责。
        </p>
        <p>
          我们不会主动查看、存储或修改远程会话中的具体内容，
          但在法律法规要求的情况下，可能依法配合相关部门进行调查。
        </p>
      </section>

      <section>
        <h2>六、服务中断与免责声明</h2>
        <p>
          因系统维护、升级、网络故障、不可抗力等原因导致服务暂时中断的，
          我们将尽合理努力减少影响，但不对因此产生的损失承担责任。
        </p>
      </section>

      <section>
        <h2>七、知识产权</h2>
        <p>
          GoDesk 及其相关内容（包括但不限于软件、界面、标识、文档）均受法律保护，
          未经许可，任何个人或组织不得擅自使用、复制或传播。
        </p>
      </section>

      <section>
        <h2>八、条款变更</h2>
        <p>
          我们有权根据实际情况对本条款进行修订。
          修订后的条款将在本页面公布并生效。
        </p>
      </section>

      <section>
        <h2>九、适用法律与争议解决</h2>
        <p>
          本条款的订立、执行与解释适用相关法律法规。
          因本条款产生的争议，应通过友好协商解决。
        </p>
      </section>
    </div>
  </div>
</template>

<style scoped>
.policy-page {
  max-width: 960px;
  margin: 0 auto;
  padding: 24px;
}
.policy-card {
  padding: 32px;
}
.back-link {
  margin-bottom: 16px;
}
.policy-card h1 {
  font-family: var(--font-tech);
  letter-spacing: 0.05em;
  font-size: 28px;
  margin-bottom: 8px;
  color: var(--text);
}
.update-time {
  color: var(--muted);
  font-size: 14px;
}
.policy-card h2 {
  margin-top: 28px;
  color: var(--text);
}
.policy-card p,
.policy-card li {
  line-height: 1.9;
  color: var(--muted);
}
</style>

