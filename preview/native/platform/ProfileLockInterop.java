// SPDX-License-Identifier: GPL-2.0-or-later
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.TimeUnit;

final class ProfileLockInterop {
    private static final int BUSY = 2;
    private static final int UNSAFE = 3;
    private static final int MISSING = 4;

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    private static Process launch(String executable, String mode, Path profile) throws Exception {
        return new ProcessBuilder(executable, mode, profile.toString())
                .redirectError(ProcessBuilder.Redirect.INHERIT).start();
    }

    private static int exit(Process process) throws Exception {
        require(process.waitFor(10, TimeUnit.SECONDS), "Native lock probe timed out");
        return process.exitValue();
    }

    private static void probe(String executable, Path profile, int expected) throws Exception {
        Process process = launch(executable, "probe", profile);
        try {
            int code = exit(process);
            require(code == expected, "Expected native lock result " + expected + ", got " + code);
        } finally {
            process.destroyForcibly();
        }
    }

    public static void main(String[] args) throws Exception {
        String executable = args[0];
        Path root = Files.createTempDirectory("biglybtc-profile-lock-");
        Path profile = root.resolve("profile \u042e & \u6e2c\u8a66");
        Path lockPath = profile.resolve(".azlock");
        Path alias = root.resolve("lock-alias");
        try {
            probe(executable, profile, MISSING);
            Files.createDirectory(profile);

            try (var javaOwner = new RandomAccessFile(lockPath.toFile(), "rw");
                 var lock = javaOwner.getChannel().tryLock()) {
                require(lock != null, "Java must acquire its original profile lock");
                probe(executable, profile, BUSY);
            }

            Process nativeOwner = launch(executable, "hold", profile);
            try {
                require("held".equals(nativeOwner.inputReader().readLine()),
                        "Native client must retain the lock after closing the original owner");
                try (var javaOwner = new RandomAccessFile(lockPath.toFile(), "rw")) {
                    require(javaOwner.getChannel().tryLock() == null,
                            "Java must refuse a profile owned by the native client");
                }
                probe(executable, profile, BUSY);
                nativeOwner.getOutputStream().write("release\n".getBytes(StandardCharsets.US_ASCII));
                nativeOwner.getOutputStream().close();
                require(exit(nativeOwner) == 0, "Closing the last native owner must succeed");
            } finally {
                nativeOwner.destroyForcibly();
            }

            try (var javaOwner = new RandomAccessFile(lockPath.toFile(), "rw");
                 var lock = javaOwner.getChannel().tryLock()) {
                require(lock != null, "Java must reopen the profile after the native owner exits");
            }

            Files.createLink(alias, lockPath);
            probe(executable, profile, UNSAFE);
            Files.delete(alias);
            Files.delete(lockPath);
            if (!System.getProperty("os.name").startsWith("Windows")) {
                Files.createFile(alias);
                Files.createSymbolicLink(lockPath, alias);
                probe(executable, profile, UNSAFE);
                Files.delete(lockPath);
                Files.delete(alias);
            }
            Files.createDirectory(lockPath);
            probe(executable, profile, UNSAFE);
            Files.delete(lockPath);
            probe(executable, profile, 0);
            System.out.println("Native and Java .azlock exclusion verified on "
                    + System.getProperty("os.name") + " " + System.getProperty("os.arch"));
        } finally {
            Files.deleteIfExists(alias);
            Files.deleteIfExists(lockPath);
            Files.deleteIfExists(profile);
            Files.deleteIfExists(root);
        }
    }
}
