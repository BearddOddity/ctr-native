import { defineConfig } from "vite";

// Tauri expects a fixed port in dev and the built files in dist/.
export default defineConfig({
  clearScreen: false,
  server: { port: 1430, strictPort: true, watch: { ignored: ["**/src-tauri/**"] } },
  build: { target: "es2022", outDir: "dist", emptyOutDir: true },
});
