import { defineConfig } from 'vite'

export default defineConfig({
  root: '.',
  build: {
    outDir:      'dist',
    emptyOutDir: true,
  },
  server: {
    port: 3000,
    proxy: {
      '/ws': { target: 'ws://localhost:8080', ws: true, rewrite: (path) => path.replace(/^\/ws/, '') },
    },
  },
  preview: {
    port: 3000,
    proxy: {
      '/ws': { target: 'ws://localhost:8080', ws: true, rewrite: (path) => path.replace(/^\/ws/, '') },
    },
  },
})
