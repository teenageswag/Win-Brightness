# Embedded Inter fonts

Source: the official [Inter 4.1 release](https://github.com/rsms/inter/releases/tag/v4.1), static TTF files from `extras/ttf` in `Inter-4.1.zip`.

Included files: `Inter-Regular.ttf` (400), `Inter-Medium.ttf` (500), `Inter-SemiBold.ttf` (600), and the release's unmodified `LICENSE.txt` (SIL Open Font License 1.1).

The resource compiler embeds the fonts and license into the executable. DirectWrite reads these immutable resource bytes through a private font collection; no system-wide installation is performed. The project also copies the license to `Inter-LICENSE.txt` next to the built executable for distribution.
