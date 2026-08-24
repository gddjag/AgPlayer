# Custom Metadata Tag Design

## Goal

Replace the metadata editor's editable `year` field with a real, batch-editable `customTag` field, rendered as `自定义标签` in the Chinese desktop UI.

## Visual Contract

- The right inspector contains this ordered list: 标题、艺术家、专辑、专辑艺术家、流派、作曲、日期、自定义标签、BPM.
- The left file table replaces its 年份 column with 标签.
- The field and table use the existing geometry, typography, colors, and direct-entry behavior; only the label and data source change.
- The summary lists 自定义标签 when it is set or cleared. 年份 is absent from the metadata-editor form and summary.

## Metadata Contract

- Introduce `CanonicalField::CustomTag` with canonical key `AGPLAYER_TAG`.
- Keep the existing `Year` core field and public API intact; the replacement applies only to the metadata-editor surface.
- `customTag` uses the existing preserve/set/clear `FieldEdit` contract, validation, preflight, staged stream-copy write, backup, atomic replacement, verification, and rollback.
- The Qt/QML boundary uses the key `customTag`; the core writer uses the stable canonical key `AGPLAYER_TAG` and the existing FFmpeg metadata dictionary mechanism for container-specific serialization.
- Unsupported containers remain rejected or limited by the existing preflight rules; no container support is widened merely for this field.

## Data Flow

1. Decoder probing and the C API expose `AGPLAYER_TAG` as `custom_tag` / `ag_metadata_custom_tag()`.
2. Metadata-editor probing reads that value into `MetadataEntry::customTag`.
3. The file table, selection aggregate, and right-side direct-entry field consume that value.
4. Applying the form maps `customTag` to `CanonicalField::CustomTag`.
5. The writer verifies the value after staged output is produced before atomically replacing the source file.
6. The editor refreshes its entry from the rewritten file, while the media library refreshes the modified paths through its current path-based refresh call.

## Validation

- Core writer tests prove set, clear, and preserved custom tag behavior on a supported fixture.
- Qt metadata-editor tests prove the payload maps `customTag` and the UI no longer exposes `year` in its canonical field list.
- The reference-layout contract checks `customTag` and rejects `year` as an editable field.
- Build, QML lint, focused metadata tests, and the existing layout contract are run before completion.
