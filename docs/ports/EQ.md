# EQ port

The device-chain EQ card is a drawing only: a compact curve and band nodes, so the chain matches the warm concept. It does not filter audio and it is not the EQ editor.

The DSP, the band editor, and the factory theme files stay a separate port. Theme JSON in the DAW can already import EQ `Themes/presets.xml` and `ramp_presets.xml` (see `importEqThemes` and `importEqRamps`). The EQ repository does not commit a factory `presets.xml`; the ramp factory lives in that app's preset seed.
