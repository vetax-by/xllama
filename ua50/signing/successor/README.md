# Configurable diagnostic successor signing input

The registered manual `cpu-pilot-signing.yml` workflow binds independently reviewed unsigned Windows run `44097593756`, attempt 1, source head `57b2261833ae75c75c60c862eb42124000fc11a3`. Independent artifact QA RESULT SHA-256 `38287176a6f8add4b7cdd05d5a00e75bfae4b8e6e88410e06c8ccff037397285` accepts compile, link and unsigned packaging only. The patched MSIX SHA-256 is `1d5a34cb279ea1558ecbaf1de5bb5b29131976d0cd2095f06453ef2a083ddd13` (21,349,747 bytes); source receipt SHA-256 `fb929228cd8105d6482e5f386b3f0b3f26b2923fd246d7a14eaca3cd8cd3c071` binds patch lock `f035c9cd3b672ac2b441eecd65d171ef2175c33ea9c34045644d61c0d85957e0` and 84 selected postimages. The stock input remains the unchanged accepted run `43592901046` baseline retained as an exact archive.

Fresh GET-only predecessor readback, independently reviewed with machine RESULT SHA-256 `9e1585bc9b46247032b71d7f62cb1d4e593e9aaf008f6d4f5298b9814ea2d218`, observes installed `1.7.0.1514` and full PFN `GianlucaMazza.xllama_1.7.0.1514_x64__pj67f1fcj4n14`. This input selects patched `1.7.0.1516` and stock forward recovery `1.7.0.1517`; earlier signed `1515` remains historical. The workflow checks the exact version triplet, source and build receipts, package hashes and retained stock archive before one disposable signing operation. It performs no native compilation. Source review and live artifact availability are required before dispatch.

Run an offline preflight against the exact saved unsigned inputs before publication:

```bash
python3 ua50/signing/successor/validate_pair_inputs.py --plan ua50/signing/successor/input-template.json --input-root <saved-accepted-input-root>
```

A signed pair requires separate signature, payload and certificate QA and a reviewed preserving install route. This source input does not establish console trust, install, configurable diagnostic behavior, model2 parity or generation. The signed `1514` model2 run closed with numerical refusal and is spent. After any future install, **no application launch** is allowed until GET-only readback establishes the exact new installed version and full PFN, followed by a later explicit Stan Game confirmation for that same pair. Silence or timeout does not release this gate. The later manual launch and exit is a separate selected step.
