<script setup lang="ts">
interface Props {
  title: string
  showArea?: boolean
  xAxis?: string[]
  yAxis?: number[]
  areaColorStart?: string
  areaColorEnd?: string
}

const props = withDefaults(defineProps<Props>(), {
  showArea: true,
  xAxis: () => [],
  yAxis: () => [],
  areaColorStart: 'rgba(45,36,255,0.5)',
  areaColorEnd: 'rgba(106,36,255,0.05)',
})
const startColor = computed(() => (props.showArea ? props.areaColorStart : 'rgba(0,0,0,0)'))
const endColor = computed(() => (props.showArea ? props.areaColorEnd : 'rgba(0,0,0,0)'))
const areaColorStops = computed(() => [
  { offset: 0, color: startColor.value },
  { offset: 1, color: endColor.value },
])

import { computed, onMounted, onUnmounted, ref, watch } from 'vue'
import * as echarts from 'echarts'
const chartRef = ref(null)
let chart: echarts.ECharts | null = null

onMounted(() => {
  chart = echarts.init(chartRef.value)
  const option = {
    title: {
      text: props.title,
    },
    tooltip: {},
    xAxis: {
      type: 'category',
      data: props.xAxis || [],
    },
    yAxis: {
      type: 'value',
    },
    symbol: 'none', // 👈 关键：设置为 'none' 不显示标记点
    series: [
      {
        name: '',
        type: 'line',
        data: props.yAxis || [],
        animation: false,
        areaStyle: {
          color: {
            type: 'linear',
            x: 0,
            y: 0,
            x2: 0,
            y2: 1,
            colorStops: areaColorStops.value,
          },
        },
      },
    ],
  }
  chart.setOption(option)
})

onUnmounted(() => {
  chart?.dispose()
})

watch(
  () => [props.xAxis, props.yAxis, props.showArea, props.title],
  () => {
    if (!chart) return

    const xData = props.xAxis ?? []
    const yData = props.yAxis ?? []

    if (!xData.length || !yData.length) {
      return
    }

    chart.setOption(
      {
        title: {
          text: props.title,
        },
        xAxis: {
          data: props.xAxis,
        },
        series: [
          {
            data: props.yAxis,
            symbol: 'none',
            areaStyle: {
              color: {
                type: 'linear',
                x: 0,
                y: 0,
                x2: 0,
                y2: 1,
                colorStops: areaColorStops.value,
              },
            },
          },
        ],
      },
      //{ notMerge: true },
    )
  },
  { deep: true },
)
</script>

<template>
  <div class="!w-full !h-80 flex items-center justify-center">
    <div ref="chartRef" class="!w-full !h-80 bg-white rounded-xl pt-10"></div>
  </div>
</template>

<style scoped></style>
