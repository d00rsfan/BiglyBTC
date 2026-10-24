import java.io.File;
import java.lang.reflect.InvocationTargetException;
import java.net.URL;
import java.net.URLClassLoader;

/** Original classes never enter this parent loader; helper providers have exact allowlist precedence. */
public final class QueueRankLauncher {
    public static void main(String[] args) throws Throwable {
        if (args.length != 2 || !(System.getSecurityManager() instanceof QueueOracleFence)) {
            throw new IllegalStateException("Fenced launcher needs helper/cohort roots");
        }
        URL[] urls = {new File(args[0]).toURI().toURL(), new File(args[1]).toURI().toURL()};
        try (URLClassLoader isolated = new URLClassLoader(urls, ClassLoader.getSystemClassLoader().getParent())) {
            Class<?> subject = isolated.loadClass("com.biglybt.plugin.startstoprules.defaultplugin.RankCalculatorReal");
            File origin = new File(subject.getProtectionDomain().getCodeSource().getLocation().toURI()).getCanonicalFile();
            if (!origin.equals(new File(args[1]).getCanonicalFile())) {
                throw new IllegalStateException("Original subject shadowed by helper");
            }
            try {
                isolated.loadClass("com.biglybt.plugin.startstoprules.defaultplugin.QueueRankOracle")
                    .getMethod("main", String[].class).invoke(null, (Object)new String[0]);
            } catch (InvocationTargetException error) { throw error.getCause(); }
        }
        ((QueueOracleFence)System.getSecurityManager()).verify();
    }
}
