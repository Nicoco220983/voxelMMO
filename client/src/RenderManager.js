// @ts-check
import * as THREE from 'three'
import { EffectComposer } from 'three/examples/jsm/postprocessing/EffectComposer.js'
import { RenderPass }     from 'three/examples/jsm/postprocessing/RenderPass.js'
import { SSAOPass }       from 'three/examples/jsm/postprocessing/SSAOPass.js'
import { OutputPass }     from 'three/examples/jsm/postprocessing/OutputPass.js'

/**
 * Encapsulates Three.js renderer, scene, camera, lights, and post-processing.
 * Keeps main.js free of low-level graphics setup.
 */
export class RenderManager {
  /** @type {THREE.WebGLRenderer} */
  renderer

  /** @type {THREE.Scene} */
  scene

  /** @type {THREE.PerspectiveCamera} */
  camera

  /** @type {EffectComposer} */
  composer

  /** @type {SSAOPass} */
  ssaoPass

  constructor() {
    // ── Renderer ────────────────────────────────────────────────────────────
    this.renderer = new THREE.WebGLRenderer({ antialias: true })
    this.renderer.setSize(window.innerWidth, window.innerHeight)
    this.renderer.setPixelRatio(devicePixelRatio)
    document.body.appendChild(this.renderer.domElement)

    // ── Scene ───────────────────────────────────────────────────────────────
    this.scene = new THREE.Scene()
    // Exponential fog for better distance falloff, matches sky horizon
    this.scene.fog = new THREE.FogExp2(0x87ceeb, 0.02)

    // ── Sky ─────────────────────────────────────────────────────────────────
    this._setupSky()

    // ── Camera ──────────────────────────────────────────────────────────────
    this.camera = new THREE.PerspectiveCamera(
      75, window.innerWidth / window.innerHeight, 0.1, 1000)
    this.camera.rotation.order = 'YXZ'

    // ── Lights ──────────────────────────────────────────────────────────────
    this._setupLights()

    // ── Post-processing ─────────────────────────────────────────────────────
    this._setupPostProcessing()

    // ── Resize ──────────────────────────────────────────────────────────────
    window.addEventListener('resize', () => this._onResize())
  }

  _setupLights() {
    this.scene.add(new THREE.AmbientLight(0xffffff, 0.35))
    const sun = new THREE.DirectionalLight(0xffffff, 0.8)
    sun.position.set(200, 400, 100)
    this.scene.add(sun)
  }

  _setupSky() {
    // Large sphere with gradient shader that follows camera
    const skyGeo = new THREE.SphereGeometry(900, 32, 32)
    const skyMat = new THREE.ShaderMaterial({
      uniforms: {
        topColor:    { value: new THREE.Color(0x0077ff) },
        bottomColor: { value: new THREE.Color(0x87ceeb) },
        offset:      { value: 100 },
        exponent:    { value: 0.6 },
      },
      vertexShader: /* glsl */ `
        varying vec3 vWorldPosition;
        void main() {
          vec4 worldPosition = modelMatrix * vec4(position, 1.0);
          vWorldPosition = worldPosition.xyz;
          gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
        }
      `,
      fragmentShader: /* glsl */ `
        uniform vec3 topColor;
        uniform vec3 bottomColor;
        uniform float offset;
        uniform float exponent;
        varying vec3 vWorldPosition;
        void main() {
          float h = normalize(vWorldPosition + offset).y;
          gl_FragColor = vec4(mix(bottomColor, topColor, max(pow(max(h, 0.0), exponent), 0.0)), 1.0);
        }
      `,
      side: THREE.BackSide,
      depthWrite: false,
    })
    this.sky = new THREE.Mesh(skyGeo, skyMat)
    this.sky.frustumCulled = false
    this.scene.add(this.sky)
  }

  _setupPostProcessing() {
    this.composer = new EffectComposer(this.renderer)
    this.composer.addPass(new RenderPass(this.scene, this.camera))

    this.ssaoPass = new SSAOPass(
      this.scene, this.camera, window.innerWidth, window.innerHeight)
    this.ssaoPass.kernelRadius = 1.5
    this.ssaoPass.minDistance = 0.0001
    this.ssaoPass.maxDistance = 0.05
    this.ssaoPass.aoIntensity = 1.2

    this.ssaoPass.copyMaterial = new THREE.ShaderMaterial({
      uniforms: {
        tDiffuse:  { value: null },
        intensity: { value: this.ssaoPass.aoIntensity },
      },
      vertexShader: /* glsl */ `
        varying vec2 vUv;
        void main() {
          vUv = uv;
          gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
        }
      `,
      fragmentShader: /* glsl */ `
        uniform sampler2D tDiffuse;
        uniform float intensity;
        varying vec2 vUv;
        void main() {
          float ao = texture2D(tDiffuse, vUv).r;
          gl_FragColor = vec4(vec3(1.0 - intensity * (1.0 - ao)), 1.0);
        }
      `,
      transparent: true,
      depthTest: false,
      depthWrite: false,
      blending: THREE.CustomBlending,
      blendSrc: THREE.DstColorFactor,
      blendDst: THREE.ZeroFactor,
      blendEquation: THREE.AddEquation,
      blendSrcAlpha: THREE.DstAlphaFactor,
      blendDstAlpha: THREE.ZeroFactor,
      blendEquationAlpha: THREE.AddEquation,
    })

    this.composer.addPass(this.ssaoPass)
    this.composer.addPass(new OutputPass())
  }

  _onResize() {
    this.camera.aspect = window.innerWidth / window.innerHeight
    this.camera.updateProjectionMatrix()
    this.renderer.setSize(window.innerWidth, window.innerHeight)
    this.composer.setSize(window.innerWidth, window.innerHeight)
  }

  /** Render one frame through the post-processing composer. */
  render() {
    // Sky follows camera for infinite distance effect
    if (this.sky) {
      this.sky.position.copy(this.camera.position)
    }
    this.composer.render()
  }
}
