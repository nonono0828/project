import { defineConfig } from 'vite'
import { fileURLToPath, URL } from 'node:url'
import { rmSync } from 'node:fs'
import vue from '@vitejs/plugin-vue'
import AutoImport from 'unplugin-auto-import/vite'
import Components from 'unplugin-vue-components/vite'
import { ElementPlusResolver } from 'unplugin-vue-components/resolvers'

function pruneRejectedDesignAssets() {
  const rejectedAssets = [
    new URL('./dist/brand', import.meta.url),
    new URL('./dist/a5d680e2e7ed822a93a4dd528b940eaa.jpg', import.meta.url),
    new URL('./dist/934f22e3cb753cfb859ee0cd0b190ebe.jpg', import.meta.url),
  ]

  return {
    name: 'prune-rejected-design-assets',
    apply: 'build',
    closeBundle() {
      for (const asset of rejectedAssets) {
        rmSync(fileURLToPath(asset), { recursive: true, force: true })
      }
    },
  }
}

export default defineConfig({
  resolve: {
    alias: {
      '@': fileURLToPath(new URL('./src', import.meta.url)),
    },
  },
  plugins: [
    vue(),
    pruneRejectedDesignAssets(),
    AutoImport({
      resolvers: [ElementPlusResolver()],
    }),
    Components({
      resolvers: [ElementPlusResolver()],
    }),
  ],
  server: {
    host: '0.0.0.0',
    port: 5173,
  },
  build: {
    outDir: 'dist',
    assetsDir: 'assets',
    sourcemap: false,
  },
})
