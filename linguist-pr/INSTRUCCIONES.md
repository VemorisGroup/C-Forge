# PR para registrar C-Forge en GitHub Linguist

## Pasos

1. Fork https://github.com/github-linguist/linguist

2. Clonar tu fork:
   ```bash
   git clone https://github.com/VemorisGroup/linguist
   cd linguist
   ```

3. Agregar la entrada a `lib/linguist/languages.yml` (en orden alfabético, antes de "C#"):
   ```yaml
   C-Forge:
     type: programming
     color: "#FF6B1A"
     extensions:
     - ".cfv"
     tm_scope: source.cfv
     ace_mode: text
     website: https://c-forge.org
   ```

4. Agregar el grammar:
   ```bash
   # Copiar grammars/c-forge.json a vendor/grammars/
   # O usar: script/add-grammar https://github.com/VemorisGroup/C-Forge
   ```

5. Copiar los samples:
   ```bash
   cp samples/C-Forge/* samples/C-Forge/
   ```

6. Generar ID:
   ```bash
   script/update-ids
   ```

7. Abrir PR con título: "Add C-Forge language"
   - Linkear: https://github.com/search?type=code&q=NOT+is%3Afork+path%3A*.cfv
