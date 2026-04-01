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
    this.scene.background = new THREE.Color(0x87ceeb)
    this.scene.fog = new THREE.Fog(0x87ceeb, 200, 600)

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
    this.composer.render()
  }
}
