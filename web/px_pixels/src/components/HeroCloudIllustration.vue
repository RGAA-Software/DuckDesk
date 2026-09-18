<template>
  <svg
    class="hero-cloud-art"
    viewBox="0 0 720 560"
    fill="none"
    xmlns="http://www.w3.org/2000/svg"
    aria-hidden="true"
  >
    <defs>
      <linearGradient id="remote-canvas" x1="54" y1="104" x2="286" y2="204">
        <stop stop-color="#009A59" />
        <stop offset="1" stop-color="#38BDF8" />
      </linearGradient>
      <linearGradient id="game-sky" x1="424" y1="70" x2="676" y2="218">
        <stop stop-color="#071A14" />
        <stop offset=".55" stop-color="#123428" />
        <stop offset="1" stop-color="#312E81" />
      </linearGradient>
      <filter id="art-shadow" x="-25%" y="-30%" width="150%" height="170%">
        <feDropShadow dx="0" dy="18" stdDeviation="18" flood-color="#052E22" flood-opacity=".16" />
      </filter>
      <filter id="art-glow" x="-150%" y="-150%" width="400%" height="400%">
        <feGaussianBlur stdDeviation="5" result="blur" />
        <feMerge>
          <feMergeNode in="blur" />
          <feMergeNode in="SourceGraphic" />
        </feMerge>
      </filter>
      <clipPath id="game-viewport">
        <rect x="424" y="64" width="252" height="143" rx="12" />
      </clipPath>
      <clipPath id="render-viewport">
        <rect x="318" y="367" width="304" height="139" rx="12" />
      </clipPath>
      <clipPath id="render-reveal">
        <rect class="render-wipe" x="421" y="377" width="176" height="121" />
      </clipPath>
      <path id="remote-flow" d="M343 273C306 258 282 239 261 222" />
      <path id="game-flow" d="M381 272C419 251 445 229 470 207" />
      <path id="render-flow" d="M365 307C389 334 412 354 440 376" />
    </defs>

    <g class="network-layer">
      <path class="network-path" d="M343 273C306 258 282 239 261 222" />
      <path class="network-path" d="M381 272C419 251 445 229 470 207" />
      <path class="network-path" d="M365 307C389 334 412 354 440 376" />

      <circle class="data-packet packet-remote" r="4">
        <animateMotion dur="3.8s" repeatCount="indefinite">
          <mpath href="#remote-flow" />
        </animateMotion>
      </circle>
      <circle class="data-packet packet-game" r="4">
        <animateMotion dur="3.2s" begin="-.9s" repeatCount="indefinite">
          <mpath href="#game-flow" />
        </animateMotion>
      </circle>
      <circle class="data-packet packet-render" r="4">
        <animateMotion dur="4.2s" begin="-1.7s" repeatCount="indefinite">
          <mpath href="#render-flow" />
        </animateMotion>
      </circle>
    </g>

    <g class="scene-card remote-scene" filter="url(#art-shadow)">
      <rect class="panel" x="28" y="58" width="292" height="184" rx="18" />
      <rect class="panel-highlight" x="28.5" y="58.5" width="291" height="183" rx="17.5" />

      <g class="remote-display remote-display-primary">
        <rect class="device-shell" x="48" y="83" width="130" height="112" rx="10" />
        <rect x="55" y="90" width="116" height="91" rx="6" fill="url(#remote-canvas)" />
        <path d="M55 148L89 119L111 137L139 105L171 132V181H55V148Z" fill="#FFFFFF" fill-opacity=".24" />
        <circle cx="82" cy="112" r="9" fill="#D1FAE5" />
        <path class="canvas-line" d="M68 162H145M68 170H124" />
        <path
          class="remote-cursor"
          d="M115 120C112 119 110 121 111 124L114 141C115 145 119 146 121 143L126 135L133 133C137 132 138 128 134 126L115 120Z"
        />
        <path class="device-stand" d="M103 195V205M82 206H144" />
      </g>

      <g class="remote-display remote-display-secondary">
        <rect class="device-shell" x="201" y="101" width="99" height="78" rx="9" />
        <rect x="207" y="107" width="87" height="60" rx="5" fill="url(#remote-canvas)" />
        <path d="M207 145L231 124L245 136L267 113L294 137V167H207V145Z" fill="#FFFFFF" fill-opacity=".24" />
        <circle cx="226" cy="121" r="6" fill="#D1FAE5" />
        <path class="device-stand" d="M250 179V187M233 188H267" />
      </g>

      <path class="sync-line" d="M178 139H201" />
      <circle class="sync-pixel sync-pixel-one" cx="183" cy="139" r="3" />
      <circle class="sync-pixel sync-pixel-two" cx="191" cy="139" r="3" />
    </g>

    <g class="scene-card game-scene" filter="url(#art-shadow)">
      <rect class="panel" x="406" y="46" width="284" height="179" rx="18" />
      <g clip-path="url(#game-viewport)">
        <rect x="424" y="64" width="252" height="143" fill="url(#game-sky)" />
        <circle cx="630" cy="88" r="28" fill="#8B5CF6" fill-opacity=".2" />
        <path d="M424 135L466 105L507 134L551 92L596 130L633 109L676 139V207H424V135Z" fill="#0F5137" />
        <path d="M424 151L475 126L517 151L568 119L615 147L676 130V207H424V151Z" fill="#0A3527" />
        <path class="game-grid" d="M424 207L522 145M676 207L578 145M457 207L536 145M643 207L564 145M424 174H676M424 190H676" />
        <path class="game-tracer" d="M450 188C493 166 526 154 562 143" />
        <g class="target-silhouette">
          <circle cx="563" cy="130" r="8" />
          <path d="M550 163C551 146 555 138 563 138C571 138 575 146 576 163H550Z" />
        </g>
        <g class="game-crosshair">
          <circle cx="563" cy="143" r="16" />
          <path d="M563 120V133M563 153V166M540 143H553M573 143H586" />
          <circle cx="563" cy="143" r="2.5" />
        </g>
      </g>
      <g class="input-control">
        <path d="M441 184C443 174 450 169 461 169H477C488 169 495 174 497 184L501 199C503 208 493 213 487 206L478 195H460L451 206C445 213 435 208 437 199L441 184Z" />
        <path d="M451 184H462M456.5 178.5V189.5" />
        <circle cx="484" cy="182" r="2.5" />
        <circle cx="491" cy="188" r="2.5" />
      </g>
      <circle class="input-pulse input-pulse-one" cx="518" cy="190" r="4" />
      <circle class="input-pulse input-pulse-two" cx="532" cy="181" r="3" />
      <circle class="input-pulse input-pulse-three" cx="544" cy="170" r="2" />
    </g>

    <g class="scene-card render-scene" filter="url(#art-shadow)">
      <rect class="panel" x="299" y="345" width="342" height="181" rx="18" />
      <g clip-path="url(#render-viewport)">
        <rect class="render-canvas" x="318" y="367" width="304" height="139" />
        <path class="render-grid" d="M318 477H622M341 455H599M367 435H576M393 416H555M418 397H535M368 506L453 367M422 506L480 367M476 506L507 367M530 506L534 367M584 506L562 367" />

        <g class="wire-model">
          <path d="M456 398L510 380L570 407L581 459L536 492L478 486L444 446L456 398Z" />
          <path d="M456 398L506 422L510 380M506 422L570 407M506 422L536 492M506 422L478 486M444 446L506 422M581 459L506 422" />
          <path d="M456 398L444 446M510 380L536 492M570 407L478 486M581 459L456 398" />
        </g>

        <g class="material-model" clip-path="url(#render-reveal)">
          <path d="M456 398L510 380L506 422Z" fill="#FDE68A" />
          <path d="M510 380L570 407L506 422Z" fill="#FDBA74" />
          <path d="M570 407L581 459L506 422Z" fill="#FB923C" />
          <path d="M581 459L536 492L506 422Z" fill="#C084FC" />
          <path d="M536 492L478 486L506 422Z" fill="#9333EA" />
          <path d="M478 486L444 446L506 422Z" fill="#0EA5E9" />
          <path d="M444 446L456 398L506 422Z" fill="#22C55E" />
          <path d="M456 398L510 380L570 407L581 459L536 492L478 486L444 446Z" class="material-outline" />
        </g>
        <rect class="render-scan" x="442" y="378" width="2" height="120" rx="1" />
      </g>
      <g class="render-progress">
        <circle cx="601" cy="386" r="5" />
        <path d="M325 516H614" />
        <path class="render-progress-value" d="M325 516H614" />
      </g>
    </g>

    <g class="cloud-core">
      <circle class="core-orbit core-orbit-outer" cx="360" cy="282" r="49" />
      <circle class="core-orbit core-orbit-inner" cx="360" cy="282" r="38" />
      <circle class="core-surface" cx="360" cy="282" r="28" />
      <g class="core-mark" transform="translate(360 282) rotate(45)">
        <circle cx="-9" cy="-9" r="3.2" opacity=".35" />
        <circle cx="0" cy="-9" r="3.2" />
        <circle cx="9" cy="-9" r="3.2" opacity=".35" />
        <circle cx="-9" cy="0" r="3.2" />
        <circle cx="0" cy="0" r="3.2" opacity=".35" />
        <circle cx="9" cy="0" r="3.2" />
        <circle cx="-9" cy="9" r="3.2" opacity=".35" />
        <circle cx="0" cy="9" r="3.2" />
        <circle cx="9" cy="9" r="3.2" opacity=".35" />
      </g>
    </g>
  </svg>
</template>

<style scoped>
.hero-cloud-art {
    --art-panel: color-mix(in srgb, var(--card) 94%, transparent);
    --art-panel-edge: color-mix(in srgb, var(--primary) 30%, var(--border));
    --art-panel-highlight: color-mix(in srgb, var(--foreground) 7%, transparent);
    --art-device: #12251e;
    --art-ink: var(--accent-foreground);
    --art-muted: color-mix(in srgb, var(--primary) 32%, var(--border));
    --art-grid: color-mix(in srgb, var(--primary) 20%, transparent);
    --art-core: var(--primary);
    --art-core-light: #8ceec0;
    display: block;
    width: 100%;
    height: auto;
    overflow: visible;
}

.panel {
    fill: var(--art-panel);
    stroke: var(--art-panel-edge);
}

.panel-highlight {
    fill: none;
    stroke: var(--art-panel-highlight);
}

.scene-card {
    transform-box: fill-box;
    transform-origin: center;
    transition: transform 300ms ease;
}

.remote-scene {
    animation: float-remote 8s ease-in-out infinite;
}

.game-scene {
    animation: float-game 9.5s ease-in-out -2s infinite;
}

.render-scene {
    animation: float-render 10.5s ease-in-out -4s infinite;
}

.scene-card:hover {
    animation-play-state: paused;
    transform: translateY(-5px);
}

.device-shell {
    fill: var(--art-device);
}

.device-stand,
.canvas-line {
    fill: none;
    stroke: var(--art-ink);
    stroke-linecap: round;
    stroke-width: 3;
}

.canvas-line {
    stroke: rgba(255, 255, 255, 0.55);
    stroke-width: 2;
}

.remote-cursor {
    fill: #ffffff;
    stroke: var(--art-device);
    stroke-linejoin: round;
    stroke-width: 2;
    transform-box: fill-box;
    transform-origin: center;
    animation: cursor-travel 5.5s ease-in-out infinite;
}

.sync-line {
    fill: none;
    stroke: var(--art-muted);
    stroke-dasharray: 3 5;
    stroke-width: 2;
}

.sync-pixel {
    fill: var(--art-core);
    filter: url(#art-glow);
    animation: sync-pixel 2s ease-in-out infinite;
}

.sync-pixel-two {
    animation-delay: -1s;
}

.game-grid {
    fill: none;
    stroke: rgba(140, 238, 192, 0.22);
    stroke-width: 1;
}

.game-tracer {
    fill: none;
    stroke: #8ceec0;
    stroke-dasharray: 5 10;
    stroke-linecap: round;
    stroke-width: 2;
    animation: tracer 1.4s linear infinite;
}

.target-silhouette {
    fill: #fb7185;
    opacity: 0.78;
}

.game-crosshair {
    fill: none;
    stroke: #d1fae5;
    stroke-linecap: round;
    stroke-width: 1.5;
    transform-box: fill-box;
    transform-origin: center;
    animation: crosshair-drift 4.8s ease-in-out infinite;
}

.game-crosshair circle:last-child {
    fill: #d1fae5;
}

.input-control {
    fill: var(--art-muted);
    stroke: var(--art-ink);
    stroke-linecap: round;
    stroke-linejoin: round;
    stroke-width: 2;
}

.input-control path:nth-child(2),
.input-control circle {
    fill: none;
}

.input-pulse {
    fill: var(--art-core);
    animation: input-pulse 1.8s ease-in-out infinite;
}

.input-pulse-two {
    animation-delay: -0.45s;
}

.input-pulse-three {
    animation-delay: -0.9s;
}

.render-canvas {
    fill: color-mix(in srgb, var(--art-panel) 58%, #081b14);
}

.render-grid {
    fill: none;
    stroke: var(--art-grid);
    stroke-width: 1;
}

.wire-model {
    fill: none;
    stroke: var(--art-ink);
    stroke-linecap: round;
    stroke-linejoin: round;
    stroke-width: 1.4;
}

.material-outline {
    fill: none;
    stroke: rgba(255, 255, 255, 0.62);
    stroke-linejoin: round;
    stroke-width: 1.4;
}

.render-wipe {
    transform-box: fill-box;
    transform-origin: left center;
    animation: render-reveal 7.5s ease-in-out infinite;
}

.render-scan {
    fill: #d1fae5;
    filter: url(#art-glow);
    transform-box: fill-box;
    animation: render-scan 7.5s ease-in-out infinite;
}

.render-progress circle {
    fill: #fb923c;
}

.render-progress path {
    fill: none;
    stroke: var(--art-muted);
    stroke-linecap: round;
    stroke-width: 3;
}

.render-progress .render-progress-value {
    stroke: #fb923c;
    stroke-dasharray: 289;
    animation: render-progress 7.5s ease-in-out infinite;
}

.network-path {
    fill: none;
    stroke: var(--art-core);
    stroke-dasharray: 4 8;
    stroke-linecap: round;
    stroke-width: 1.5;
    opacity: 0.48;
    animation: network-dash 5s linear infinite;
}

.data-packet {
    fill: var(--art-core-light);
    filter: url(#art-glow);
}

.packet-game {
    fill: #a78bfa;
}

.packet-render {
    fill: #fb923c;
}

.cloud-core {
    filter: url(#art-shadow);
}

.core-orbit {
    fill: none;
    stroke: var(--art-core);
    transform-box: fill-box;
    transform-origin: center;
}

.core-orbit-outer {
    stroke-dasharray: 3 8;
    opacity: 0.42;
    animation: orbit-turn 18s linear infinite;
}

.core-orbit-inner {
    opacity: 0.2;
    animation: core-pulse 3s ease-in-out infinite;
}

.core-surface {
    fill: var(--art-panel);
    stroke: var(--art-panel-edge);
    stroke-width: 1.5;
}

.core-mark {
    fill: var(--art-core);
}

@keyframes float-remote {
    0%, 100% { transform: translate3d(0, 0, 0) rotate(-0.7deg); }
    50% { transform: translate3d(0, -7px, 0) rotate(-0.2deg); }
}

@keyframes float-game {
    0%, 100% { transform: translate3d(0, 0, 0) rotate(0.8deg); }
    50% { transform: translate3d(0, -9px, 0) rotate(0.2deg); }
}

@keyframes float-render {
    0%, 100% { transform: translate3d(0, 0, 0) rotate(-0.35deg); }
    50% { transform: translate3d(0, -6px, 0) rotate(0.25deg); }
}

@keyframes cursor-travel {
    0%, 18%, 100% { transform: translate(0, 0); }
    48%, 68% { transform: translate(24px, 18px); }
}

@keyframes sync-pixel {
    0%, 100% { opacity: 0.24; transform: translateX(0); }
    50% { opacity: 1; transform: translateX(5px); }
}

@keyframes tracer {
    to { stroke-dashoffset: -30; }
}

@keyframes crosshair-drift {
    0%, 100% { transform: translate(0, 0); }
    45% { transform: translate(-9px, 4px); }
    70% { transform: translate(4px, -3px); }
}

@keyframes input-pulse {
    0%, 100% { opacity: 0.2; }
    50% { opacity: 1; }
}

@keyframes render-reveal {
    0%, 12%, 100% { transform: scaleX(0.08); }
    48%, 78% { transform: scaleX(1); }
}

@keyframes render-scan {
    0%, 12%, 100% { transform: translateX(0); opacity: 0.25; }
    48%, 78% { transform: translateX(137px); opacity: 1; }
}

@keyframes render-progress {
    0%, 12%, 100% { stroke-dashoffset: 270; }
    48%, 78% { stroke-dashoffset: 0; }
}

@keyframes network-dash {
    to { stroke-dashoffset: -48; }
}

@keyframes orbit-turn {
    to { transform: rotate(360deg); }
}

@keyframes core-pulse {
    0%, 100% { opacity: 0.18; transform: scale(0.94); }
    50% { opacity: 0.42; transform: scale(1.08); }
}

@media (prefers-reduced-motion: reduce) {
    .hero-cloud-art *,
    .hero-cloud-art *::before,
    .hero-cloud-art *::after {
        animation: none !important;
        transition: none !important;
    }

    .data-packet,
    .render-scan {
        display: none;
    }
}
</style>
