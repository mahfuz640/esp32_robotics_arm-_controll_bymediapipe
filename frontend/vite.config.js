import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
  server: { proxy: { '/api': 'http://localhost:5000' } },
  build: {
    rollupOptions: {
      output: {
        manualChunks: {
          'three-vendor': ['three'],
          'motion-vendor': ['gsap'],
          'vision-vendor': ['@mediapipe/tasks-vision'],
        },
      },
    },
  },
})
