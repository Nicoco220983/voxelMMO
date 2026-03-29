import { defineConfig } from 'vite'

export default defineConfig({
  root: '.',
  publicDir: 'static',
  build: {
    outDir:      'dist',
    emptyOutDir: true,
    sourcemap:   true,
  },
  server: {
    port: 3000,
    host: true,
    proxy: {
      '/ws': { target: 'ws://localhost:8080', ws: true, rewrite: (path) => path.replace(/^\/ws/, '') },
    },
  },
  preview: {
    port: 3000,
    host: true,
    proxy: {
      '/ws': { target: 'ws://localhost:8080', ws: true, rewrite: (path) => path.replace(/^\/ws/, '') },
    },
  },
})
