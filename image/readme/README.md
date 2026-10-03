# README artwork

The English and Russian READMEs share the hero and native screenshots. The mode diagrams have separate labels for each language.

| Asset | Source |
| :--- | :--- |
| `hero.svg` | Original vector monitor illustration with an embedded `expanded-dark.png` frame from the production renderer |
| `modes-en.svg`, `modes-ru.svg` | Original vector diagrams explaining software overlays and DDC/CI control |
| `hardware.png` | Unmodified `hardware-light.png` export from `RendererTests` |
| `../island-dark.png`, `../island-light.png` | Native two-display renderer exports, used in the theme comparison |

The display names, availability, and brightness values in these frames are synthetic test data. They are not hardware compatibility results.

To regenerate these assets after building the [test suite](../../tests/README.md), run from the repository root:

```powershell
New-Item -ItemType Directory -Force build/readme-preview
& build/tests/Release/RendererTests.exe "$PWD/build/readme-preview"
node docs/render-readme-assets.mjs build/readme-preview
Copy-Item build/readme-preview/expanded-dark.png image/island-dark.png
Copy-Item build/readme-preview/expanded-light.png image/island-light.png
```

The generator uses only Node.js built-in modules. Vector sources live in [docs/render-readme-assets.mjs](../../docs/render-readme-assets.mjs); it embeds the native panel frame so the hero has no external image dependency.
