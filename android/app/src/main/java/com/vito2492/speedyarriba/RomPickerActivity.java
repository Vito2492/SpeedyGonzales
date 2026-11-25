package com.vito2492.speedyarriba;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

public class RomPickerActivity extends Activity {
    private static final int REQ_ROM = 1001;
    private static final int ROM_SIZE = 0x100000;

    private File targetRomFile() {
        return new File(getFilesDir(), "rom.sfc");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (targetRomFile().isFile() && targetRomFile().length() == ROM_SIZE) {
            launchGame();
            return;
        }
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setBackgroundColor(Color.parseColor("#240C10"));
        root.setPadding(64, 48, 64, 48);

        TextView title = new TextView(this);
        title.setText("Speedy Gonzales: Arriba Edition");
        title.setTextColor(Color.parseColor("#FFD840"));
        title.setTextSize(24f);
        title.setGravity(Gravity.CENTER);
        root.addView(title);

        TextView msg = new TextView(this);
        msg.setText("\nEnlaza tu ROM dumpeada legalmente de\nSpeedy Gonzales - Los Gatos Bandidos (USA) (.sfc / .smc).\nLa ROM se valida localmente y nunca se modifica ni redistribuye.\n");
        msg.setTextColor(Color.WHITE);
        msg.setTextSize(15f);
        msg.setGravity(Gravity.CENTER);
        root.addView(msg);

        Button btn = new Button(this);
        btn.setText("SELECCIONAR ROM (.sfc / .smc)");
        btn.setBackgroundColor(Color.parseColor("#C83020"));
        btn.setTextColor(Color.parseColor("#FFD840"));
        btn.setPadding(48, 28, 48, 28);
        btn.setOnClickListener(v -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            startActivityForResult(i, REQ_ROM);
        });
        root.addView(btn);
        setContentView(root);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != REQ_ROM || res != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        try (InputStream in = getContentResolver().openInputStream(uri)) {
            if (in == null) throw new Exception("No se pudo abrir");
            byte[] buf = in.readAllBytes();
            int off = (buf.length == ROM_SIZE + 512) ? 512 : 0;
            if (buf.length - off != ROM_SIZE) {
                Toast.makeText(this, "Tamano invalido (esperado 1 MB)", Toast.LENGTH_LONG).show();
                return;
            }
            String hdr = new String(buf, off + 0x7FC0, 15, StandardCharsets.US_ASCII);
            if (!hdr.equals("Speedy Gonzales")) {
                Toast.makeText(this, "No es Speedy Gonzales (USA) SNES", Toast.LENGTH_LONG).show();
                return;
            }
            try (FileOutputStream out = new FileOutputStream(targetRomFile())) {
                out.write(buf, off, ROM_SIZE);
            }
            launchGame();
        } catch (Exception e) {
            Toast.makeText(this, "Error leyendo ROM: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

    private void launchGame() {
        startActivity(new Intent(this, SpeedyActivity.class));
        finish();
    }
}
