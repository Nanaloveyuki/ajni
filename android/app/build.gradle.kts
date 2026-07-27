import org.jetbrains.kotlin.gradle.dsl.JvmTarget

val configuredNdkPath = providers.environmentVariable("ANDROID_NDK_HOME").orNull

plugins {
  id("com.android.application")
  id("org.jetbrains.kotlin.android")
}

android {
  namespace = "dev.nanaloveyuki.ajni.demo"
  compileSdk = 35
  buildToolsVersion = "36.1.0"
  if (configuredNdkPath != null) {
    ndkPath = configuredNdkPath
  }
  ndkVersion = "29.0.14206865"

  defaultConfig {
    applicationId = "dev.nanaloveyuki.ajni.demo"
    minSdk = 24
    targetSdk = 35
    versionCode = 1
    versionName = "0.1.1"

    testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    externalNativeBuild {
      cmake {
        cppFlags += listOf("-std=c11")
      }
    }
    ndk {
      abiFilters += listOf("arm64-v8a", "x86_64")
    }
  }

  buildTypes {
    release {
      isMinifyEnabled = false
    }
  }

  compileOptions {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
  }

  externalNativeBuild {
    cmake {
      path = file("src/main/cpp/CMakeLists.txt")
      version = "4.1.2"
    }
  }
}

kotlin {
  compilerOptions {
    jvmTarget.set(JvmTarget.JVM_17)
  }
}

dependencies {
  implementation(project(":host"))
  implementation("androidx.activity:activity-ktx:1.9.3")
  implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.7")
  implementation("androidx.lifecycle:lifecycle-process:2.8.7")
  androidTestImplementation("androidx.test.ext:junit:1.2.1")
  androidTestImplementation("androidx.test:core:1.6.1")
  androidTestImplementation("androidx.test.espresso:espresso-core:3.6.1")
  androidTestImplementation("androidx.test:runner:1.6.2")
}
