# SDK publication status

Current source layout and registry coverage. A GitHub download is distinct from publication on a central registry.

| Target | Status | Package / documentation |
| --- | --- | --- |
| CPP/vcpkg | published_custom_registry | vdevice |
| CPP/conan | public_recipe_builds_locally |  [Source](packaging/native/) |
| CPP/nuget | paused_pending_owner_email_confirmation | Vdevice.Native |
| NET/nuget | built_verified_publication_on_hold_by_owner | Vdevice.SDK [Source](Vdevice-NET-nuget/) |
| Python/pypi | built_fixture_and_production_verified | vdevice [Source](Vdevice-Python-pypi/) |
| Python/conda-forge | built_tested_requires_source_publication_and_official_review | vdevice [Source](packaging/conda/) |
| Rust/cargo | published_install_verified | [vdevice](https://crates.io/crates/vdevice) [Source](Vdevice-Rust-cargo/) |
| JavaScript/npm | awaiting_browser_publication_authentication | [vdevice](https://www.npmjs.com/package/vdevice) [Source](Vdevice-JavaScript-npm/) |
| Java/maven | published_install_verified | [vdevice](https://central.sonatype.com/artifact/io.github.titancsa/vdevice/1.0.0) [Source](Vdevice-Java-maven/) |
| Java/jitpack | published_install_verified | [vdevice](https://jitpack.io/#TitanCSA/Vdevice/java-sdk-v1.0.0) [Source](Vdevice-Java-maven/) |
| Go/modules | published_install_verified | [vdevice](https://pkg.go.dev/github.com/TitanCSA/Vdevice/Vdevice-Go-modules@v1.0.0) [Source](Vdevice-Go-modules/) |
| Go/pkg-go-dev | indexed_verified | [Documentation](https://pkg.go.dev/github.com/TitanCSA/Vdevice/Vdevice-Go-modules@v1.0.0) |
| PowerShell/gallery | published_install_verified | [Vdevice](https://www.powershellgallery.com/packages/Vdevice/1.0.0) [Source](Vdevice-PowerShell-gallery/) |
| Dart/pub-dev | built_fixture_and_production_verified | vdevice [Source](Vdevice-Dart-pub-dev/) |
| Ruby/rubygems | published_install_verified | [vdevice](https://rubygems.org/gems/vdevice) [Source](Vdevice-Ruby-rubygems/) |
| PHP/packagist | composer_source_and_tag_published | [titancsa/vdevice](https://packagist.org/packages/titancsa/vdevice) |

Native SDK source and CMake/Conan/vcpkg/NuGet recipes share [`packaging/native`](packaging/native/LAYOUT.md). Previous release archives and tags remain unchanged.

Application and driver prerequisites and original licensing remain those documented by each SDK. No driver installer is bundled with these source packages.
