# Consolidated diagnostic successor signing input

The registered manual `cpu-pilot-signing.yml` workflow binds independently reviewed unsigned Windows run `44149184202`, attempt 1, source head `c98fed1fa155503a521ad9355f4cd70e6b887d9f`. Independent artifact QA RESULT SHA-256 `73dbdd1016844f0bd94826d2e46c8fe89a833824e58f6d5e9f2836862f45c456` accepts compile, link and unsigned packaging only. The patched MSIX SHA-256 is `b98a74afd6d85bdb8a15531f797fe2c8ef195fb8d5a16c2a466d6f471685031d` (21,390,396 bytes); source receipt SHA-256 `ce09d86081d99aa1365cd12bfbc9ff2f08c5ae0afb1a54d280ce5538dfc3c9ba` binds patch lock `75da7c5e339b7b5528d688dbbdd4d8aa2e031ac33cda36d3f33e4eb8971a02c0` and 90 selected postimages. The stock input is the unchanged accepted run `43592901046` baseline retained as an exact archive.

Fresh GET-only predecessor readback, independently reviewed with machine RESULT SHA-256 `d778269450b5ff1112974663476b505f39986be4d71792017758fcf2813d5903`, observes installed `1.7.0.1516` and full PFN `GianlucaMazza.xllama_1.7.0.1516_x64__pj67f1fcj4n14`. This input selects patched `1.7.0.1518` and stock forward recovery `1.7.0.1519`; earlier signed `1517` remains historical. The workflow checks the exact version triplet, source and build receipts, package hashes and retained stock archive before one disposable signing operation. It performs no native compilation. Source review and live artifact availability are required before dispatch.

Run an offline preflight against the exact saved unsigned inputs before publication:

```bash
python3 ua50/signing/successor/validate_pair_inputs.py --plan ua50/signing/successor/input-template.json --input-root <saved-accepted-input-root>
```

A signed pair requires separate signature, payload and certificate QA and a reviewed preserving install route. This source input does not establish console trust, install, consolidated diagnostic behavior, model2 parity or generation. The signed `1516` model2 broad and detail runs closed with numerical refusal and are spent. After any future install, **no application launch** is allowed until GET-only readback establishes the exact new installed version and full PFN, followed by a later explicit Stan Game confirmation for that same pair. Silence or timeout does not release this gate. The later manual launch and exit is a separate selected step.
