import com.biglybt.core.config.COConfigurationManager;
import java.io.File;
import java.util.Arrays;

/** Actual pinned original configuration loader/getters/saver; no core startup. */
public final class SettingsOracle {
    private static void require(boolean value, String message) {
        if (!value) { throw new IllegalStateException(message); }
    }
    public static void main(String[] args) {
        try {
            require(System.getSecurityManager() instanceof ReferenceFence, "Owned reference fence required");
            require(args.length == 1, "create|check required");
            System.out.println("SETTINGS_BEFORE_INITIALISE");
            COConfigurationManager.initialise();
            System.out.println("SETTINGS_AFTER_INITIALISE");
            if (args[0].equals("create")) {
                COConfigurationManager.setParameter("Max Upload Speed KBs", 17);
                COConfigurationManager.setParameter("Max Download Speed KBs", 31);
                COConfigurationManager.setParameter("Proxy.Host", "127.0.0.1");
                COConfigurationManager.setParameter("Proxy.Port", "19123");
                COConfigurationManager.setParameter("Proxy.Password", "original-test-secret");
                COConfigurationManager.setParameter("native.options.opaque", Arrays.asList(Long.valueOf(7), "opaque–π"));
                COConfigurationManager.setParameter("Enable.Proxy", false);
                COConfigurationManager.setParameter("Proxy.Data.Enable", false);
                COConfigurationManager.setParameter("network.transport.encrypted.require", false);
            } else {
                require(args[0].equals("check"), "Unknown phase");
                require(COConfigurationManager.getIntParameter("Max Upload Speed KBs") == 23, "Native upload limit not read by original");
                require(COConfigurationManager.getIntParameter("Max Download Speed KBs") == 47, "Native download limit not read by original");
                require(COConfigurationManager.getIntParameter("max active torrents") == 6, "Native active cap not read by original");
                require(COConfigurationManager.getIntParameter("max downloads") == 3, "Native download cap not read by original");
                require(COConfigurationManager.getStringParameter("Proxy.Password").equals("native-test-secret"), "Native string not read by original");
                String expected = new File(System.getProperty("biglybtc.test.root"), "payload").getCanonicalPath();
                require(COConfigurationManager.getStringParameter("Default save path").equals(expected), "Native default directory not read by original");
                require(COConfigurationManager.getListParameter("native.options.opaque", null).size() == 2, "Unknown list lost");
                COConfigurationManager.setParameter("Max Upload Speed KBs", 29);
                COConfigurationManager.setParameter("network.transport.encrypted.require", true);
            }
            COConfigurationManager.save();
            require(System.getSecurityManager() instanceof ReferenceFence, "Reference fence changed");
            System.out.println("SETTINGS_ORIGINAL_" + args[0].toUpperCase() + "_PASS");
            System.exit(0);
        } catch (Throwable failure) {
            failure.printStackTrace(); System.exit(2);
        }
    }
}
