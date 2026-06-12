<script setup lang="ts">
import { RouterLink, RouterView } from 'vue-router'
// import HelloWorld from './components/HelloWorld.vue'
import MainPage from '@/views/MainPage.vue'
</script>

<template>
  <div>
<!--    <h1 class="!text-3xl !font-bold underline">-->
<!--      Hello world!-->
<!--    </h1>-->

<!--    <div>-->
<!--      <MainPage />-->
<!--    </div>-->

<!--    <div>-->
<!--      <HelloWorld msg="You did it!" />-->

<!--      <nav>-->
<!--        <RouterLink to="/">Home</RouterLink>-->
<!--        <RouterLink to="/about">About</RouterLink>-->
<!--      </nav>-->
<!--    </div>-->

    <RouterView />
  </div>
</template>
