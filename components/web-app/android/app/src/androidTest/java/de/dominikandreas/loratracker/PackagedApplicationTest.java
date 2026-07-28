package de.dominikandreas.loratracker;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;

import android.content.Context;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import java.io.InputStream;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public class PackagedApplicationTest {
    @Test
    public void packageAndOfflineApplicationAssetsArePresent() throws Exception {
        Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        assertEquals("de.dominikandreas.loratracker", context.getPackageName());

        try (InputStream dashboard = context.getAssets().open("public/index.html");
             InputStream lab = context.getAssets().open("public/lab/index.html");
             InputStream config = context.getAssets().open("capacitor.config.json")) {
            assertNotNull(dashboard);
            assertNotNull(lab);
            assertNotNull(config);
        }
    }
}
