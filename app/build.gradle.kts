plugins {
    id("com.android.application")
}

android {
    /*sourceSets["main"].assets.srcDir(
        layout.buildDirectory.dir("generated/assets")
    )
    */
    packaging {
      jniLibs {
        useLegacyPackaging = true
      }
    }
    namespace = "com.example.egl"
    compileSdk = 35
    ndkVersion = "27.1.12297006"
    ndkPath = "/data/data/com.termux/files/home/opt/android-sdk/ndk"
    defaultConfig {
        applicationId = "com.example.egl"
        minSdk = 29
        targetSdk = 35

        ndk {
            abiFilters += "arm64-v8a" // change if needed
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_static",
                    "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON"
                )
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }
}

//val FONT_NAME: String = project.findProperty("FONT_NAME") as String? ?: "Roboto-Regular.ttf"
/*val fontName = providers.gradleProperty("FONT_NAME")
    .orElse("Roboto-Regular.ttf")

val genFontAssets = tasks.register<Exec>("genFontAssets") {
    val outDir = layout.buildDirectory.dir("generated/assets/fonts")

    inputs.property("FONT_NAME", fontName)
    outputs.dir(outDir)

    commandLine(
        "bash",
        "${projectDir}/src/main/tools/copyfont.sh",
        outDir.get().asFile.absolutePath,
        fontName.get()
    )
}
*/
/*
tasks.named("preBuild").configure {
    dependsOn(genFontAssets)
}*/
