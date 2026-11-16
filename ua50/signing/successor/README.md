# UA50 model2 route successor signing input

The registered, manually dispatched `cpu-pilot-signing.yml` workflow binds the
accepted unsigned Windows run `43952468463` (head
`593340f2f4463d0089733960a16f6c97635c79e8`, attempt 1). Independent
artifact QA accepted compile, link and package only; its RESULT SHA-256 is
`2c2d1134568ad611c00cd84be8b10d4aef0476d4e1f156caa1fdf7f5ebcbd4a8`.
The unsigned MSIX SHA-256 is
`e72e73aa697aa7c4b85328d0177aa82950a951d8cd71adfbf6564b1995174fd1`.
Its source receipt binds patch lock
`fa3ce18df56361f2b78ce8eda57fdb8c87ff0d21792693b4be928415b33c0027`
and 79 selected postimages. The stock input is the unchanged accepted
run `43592901046` baseline, retained as an exact archive.

Fresh read-only installed-version evidence selected `1.7.0.1508`. This input
selects patched `1.7.0.1510` and stock forward-recovery `1.7.0.1511`. The
workflow checks the exact version triplet, source and build receipts, package
hashes and retained stock archive before one disposable signing operation.
`package_copy.py` and `sign_ephemeral_pair.ps1` remain byte-identical to the
prior successor recipe; the validator and signing helper advance only the
current lock guard and diagnostic. The helper permits only the selected
`Identity.Version` change in the application payload, subject to its bounded
SDK CodeIntegrity catalog rule.

The signing workflow creates a new ephemeral certificate. A successful run
would require independent signed-artifact QA, exact certificate trust evidence
and a separate device install route. No signed, installed or native model2
acceptance follows from this source input. The old signed `1508` model2 refusal
and its spent run remain unchanged.

The patched Actions artifact has one-day retention. If the exact run artifact
has expired, the workflow refuses; the accepted unsigned archive is also
retained in private evidence for a separately reviewed transport route. Do
not rebuild or reuse an old signing invocation to replace an expired download.
