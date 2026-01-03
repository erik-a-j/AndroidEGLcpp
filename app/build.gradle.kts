plugins {
    id("com.android.application")
}

android {
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
        minSdk = 21
        targetSdk = 35

        ndk {
            abiFilters += "arm64-v8a" // change if needed
        }

        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static", "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON")
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
