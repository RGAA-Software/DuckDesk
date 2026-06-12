<script setup lang="ts">
import rough from 'roughjs'
import { onMounted, ref } from 'vue'

const myCanvas = ref<HTMLCanvasElement | null>(null)
let index = 0

onMounted(() => {
  drawCircles(-1)
})

function drawCircles(selectedIndex: number) {
  if (myCanvas.value !== null) {
    let context = myCanvas.value.getContext('2d')
    if (context === null) {
      console.log("context for 2d is null");
      return;
    }
    context.fillStyle = '#ffffff'
    context.fillRect(0, 0, 1024, 360)

    const rc = rough.canvas(myCanvas.value)

    let y_offset = 10
    let y_gap = 30
    for (let x = 0; x < 25; x += 1) {
      for (let y = 0; y < 2; y += 1) {
        let style = ''
        let color = ''
        if (y == 0) {
          style = 'zigzag'
          color = '#2290ff'
        } else if (y == 1) {
          style = 'dots'
          color = '#2290cc'
        }

        if (selectedIndex != -1) {
          if (y == 0) {
            if (x == selectedIndex) {
              color = '#f25555'
              style = 'solid'
            } else {
              style = 'zigzag'
            }
          } else {
            if (25 - x - 1 == selectedIndex) {
              color = '#f25555'
              style = 'solid'
            } else {
            }
          }
        }
        //rc.circle(x * 40 + 20, y * (y_gap + y_offset) + 30, 15, { fill: '#2290ff', fillStyle: style }) // fill with red hachure
        if (y === 0) {
          rc.circle(x * 40 + 10, y * (y_gap + y_offset) + 40, 15, {
            roughness: 0.1,
            fill: color,
            fillStyle: style,
          })
        }
        else {
          rc.circle(x * 40 + 10, y * (y_gap + y_offset) + 40, 15, {
            roughness: 0.2,
            fill: color,
            fillStyle: style,
          })
        }
      }
    }
  }
}

const canvas = document.querySelector('canvas')
console.log('canvas', canvas)

setInterval(() => {
  drawCircles(index)
  index++
  if (index >= 25) {
    index = 0
  }
}, 500)
</script>

<template>
  <div>
    <canvas ref="myCanvas" width="1024" height="120"></canvas>
  </div>
</template>

<style scoped></style>
