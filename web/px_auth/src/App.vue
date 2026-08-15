<script setup lang="ts">
import {RouterLink, RouterView} from 'vue-router'
import LoginPanel from '@/views/LoginPanel.vue'
import MainPanel from "@/views/MainPanel.vue";

</script>

<template>
  <header>
<!--    <img alt="Vue logo" class="logo" src="@/assets/logo.svg" width="125" height="125"/>-->
  </header>

  <div class="wrapper">


    <nav>
      <!--        <RouterLink to="/">Home</RouterLink>-->
      <!--        <RouterLink to="/about">About</RouterLink>-->
    </nav>
  </div>

  <RouterView/>
</template>
