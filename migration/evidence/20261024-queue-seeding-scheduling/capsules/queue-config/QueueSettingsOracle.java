import com.biglybt.core.config.COConfigurationManager;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.List;

/** Actual original config loader/getters/saver. No core/plugin/network startup. */
public final class QueueSettingsOracle {
    private static final String ZERO_PEERS = "StartStopManager_bFirstPriority_ignore0Peer";
    private static void require(boolean value, String message) {
        if (!value) { throw new IllegalStateException(message); }
    }
    private static void unchanged() {
        require(COConfigurationManager.getStringParameter("ui").equals("az2"), "Original UI mode lost");
        // Exact StartStopConfigModel conditional default, installed through the
        // original defaults API without constructing its unrelated SWT model.
        COConfigurationManager.setBooleanDefault(ZERO_PEERS,
            !COConfigurationManager.getStringParameter("ui", "").equals("az2"));
        require(!COConfigurationManager.getBooleanParameter(ZERO_PEERS), "Conditional az2 FP default not false");
        require(!COConfigurationManager.hasParameter(ZERO_PEERS, true), "Conditional default materialized");
        List<?> opaque = COConfigurationManager.getListParameter("native.queue.opaque", null);
        require(opaque != null && opaque.size() == 2 && opaque.get(0).equals(Long.valueOf(7)), "Original opaque list lost");
        Object value = opaque.get(1);
        String text = value instanceof byte[] ? new String((byte[]) value, StandardCharsets.UTF_8) : String.valueOf(value);
        require(text.equals("opaque–π"), "Original opaque Unicode lost");
    }
    public static void main(String[] args) {
        try {
            require(System.getSecurityManager() instanceof ReferenceFence, "Owned reference fence required");
            require(args.length == 1, "create|check required");
            // Preserve the bootstrap proven by the existing actual Settings oracle.
            Class.forName("com.biglybt.core.util.Debug");
            System.out.println("QUEUE_SETTINGS_BEFORE_INITIALISE");
            COConfigurationManager.initialise();
            System.out.println("QUEUE_SETTINGS_AFTER_INITIALISE");
            if (args[0].equals("create")) {
                COConfigurationManager.setParameter("ui", "az2");
                COConfigurationManager.setParameter("StartStopManager_iRankType", 1);
                COConfigurationManager.setParameter("Stop Ratio", 0.5F);
                COConfigurationManager.setParameter("StartStopManager_iFirstPriority_ShareRatio", 500);
                COConfigurationManager.setParameter("StartStopManager_iMinSeedingTime", 300);
                COConfigurationManager.setParameter("StartStopManager_bRetainForceStartWhenComplete", false);
                COConfigurationManager.removeParameter(ZERO_PEERS);
                COConfigurationManager.setParameter("native.queue.opaque", Arrays.asList(Long.valueOf(7), "opaque–π"));
            } else {
                require(args[0].equals("check"), "Unknown phase");
                require(COConfigurationManager.getIntParameter("StartStopManager_iRankType") == 3, "Native rank mode not read by original");
                require(Float.floatToIntBits(COConfigurationManager.getFloatParameter("Stop Ratio")) == Float.floatToIntBits(1.25F), "Native Float bytes not read by original");
                require(COConfigurationManager.getIntParameter("StartStopManager_iFirstPriority_ShareRatio") == 750, "Native first priority not read by original");
                require(COConfigurationManager.getIntParameter("StartStopManager_iMinSeedingTime") == 45, "Native minimum time not read by original");
                require(COConfigurationManager.getBooleanParameter("StartStopManager_bRetainForceStartWhenComplete"), "Native Boolean not read by original");
                COConfigurationManager.setParameter("StartStopManager_iRankType", 2);
                COConfigurationManager.setParameter("Stop Ratio", 2.75F);
                COConfigurationManager.setParameter("StartStopManager_iFirstPriority_ShareRatio", 900);
            }
            unchanged();
            COConfigurationManager.save();
            require(System.getSecurityManager() instanceof ReferenceFence, "Reference fence changed");
            System.out.println("QUEUE_SETTINGS_ORIGINAL_" + args[0].toUpperCase() + "_PASS");
            System.exit(0);
        } catch (Throwable failure) {
            failure.printStackTrace(); System.exit(2);
        }
    }
}
