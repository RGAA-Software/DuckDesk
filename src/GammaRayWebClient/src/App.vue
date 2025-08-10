<script setup lang="ts">
    import Button from 'primevue/button'; // 导入 Button 组件
    // export default {
    //     components: {
    //         Button // 注册 Button 组件
    //     }
    // }
</script>

<template>
    <Panel style="position: absolute; width: 100vw; height: 100vh; overflow: hidden;">

        <Button class="!border-2" rounded
            style="position: absolute; top: 100px; left: 200px; z-index: 10; width: 45px; height: 45px;"
            @click="onClick"
            >
            <template #icon>
                <img
                    src="@/assets/icons/tc_trans_icon_white.png"
                    alt="Custom Icon"
                    style="width: 16px; height: 16px;"
                />
            </template>
        </Button>

    </Panel>
    <canvas id="main_view"></canvas>
    <video id="remoteVideo" width="960px" height="540px" autoplay muted></video>
</template>
