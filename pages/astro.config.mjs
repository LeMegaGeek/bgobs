// @ts-check
import { defineConfig } from "astro/config";

export default defineConfig({
  site: process.env.SITE ?? "https://lemegageek.github.io",
  base: process.env.BASE ?? "/bgobs",
  trailingSlash: "always",
});
