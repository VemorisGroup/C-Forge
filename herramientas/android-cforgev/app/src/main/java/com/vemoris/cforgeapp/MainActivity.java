package com.vemoris.cforgeapp;

import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;
import androidx.appcompat.app.AppCompatActivity;

/**
 * MainActivity — Ejemplo de app Android que embebe C-Forge.
 *
 * La logica de la app vive en assets/scripts/main.cfv.
 * El Activity solo hace bridge entre la UI Android y el interprete.
 */
public class MainActivity extends AppCompatActivity {

    private TextView  tvOutput;
    private EditText  etCode;
    private TextView  tvVersion;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        tvOutput  = findViewById(R.id.tvOutput);
        etCode    = findViewById(R.id.etCode);
        tvVersion = findViewById(R.id.tvVersion);

        // Mostrar version del interprete
        String version = CForgeRuntime.getVersion();
        tvVersion.setText("C-Forge v" + version);

        // Ejecutar script principal de la app (en background)
        new Thread(() -> {
            boolean ok = CForgeRuntime.runAsset(this, "scripts/main.cfv");
            runOnUiThread(() -> {
                if (!ok) tvOutput.setText("Error al cargar main.cfv");
            });
        }).start();

        // Boton: ejecutar codigo del EditText
        Button btnRun = findViewById(R.id.btnRun);
        btnRun.setOnClickListener(v -> {
            String code = etCode.getText().toString().trim();
            if (code.isEmpty()) return;
            new Thread(() -> {
                String result = CForgeRuntime.evalJson(code);
                runOnUiThread(() -> tvOutput.setText(result));
            }).start();
        });

        // Boton: llamar funcion on_boton_presionado() del script
        Button btnAction = findViewById(R.id.btnAction);
        btnAction.setOnClickListener(v -> {
            new Thread(() -> {
                String result = CForgeRuntime.evalString("on_boton_presionado()");
                runOnUiThread(() -> tvOutput.setText(result));
            }).start();
        });

        // Boton: limpiar output
        Button btnClear = findViewById(R.id.btnClear);
        btnClear.setOnClickListener(v -> tvOutput.setText(""));
    }
}
