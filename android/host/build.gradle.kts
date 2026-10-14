import org.jetbrains.kotlin.gradle.tasks.KotlinJvmCompile

plugins {
  id("com.android.library")
  id("org.jetbrains.kotlin.android")
}

android {
  namespace = "dev.nanaloveyuki.ajni.host"
  compileSdk = 35
  buildToolsVersion = "36.1.0"

  defaultConfig {
    minSdk = 24
    consumerProguardFiles("consumer-rules.pro")
  }

  compileOptions {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
  }
}

kotlin {
  compilerOptions {
    jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
  }
}

dependencies {
  api("androidx.webkit:webkit:1.12.1")
}

val repositoryRoot = rootProject.projectDir.parentFile

// AGP registers variant compilation tasks after project evaluation.
afterEvaluate {
  val compileDebugKotlin = tasks.named<KotlinJvmCompile>("compileDebugKotlin")
  tasks.register<Exec>("checkJniContract") {
    group = "verification"
    description = "Check compiled JNI signatures, C definitions, and consumer keep rules."
    dependsOn(compileDebugKotlin)
    workingDir(repositoryRoot)
    doFirst {
      val ndk = providers.environmentVariable("ANDROID_NDK_HOME").orNull
        ?: throw GradleException("checkJniContract requires ANDROID_NDK_HOME")
      val moonHome = providers.environmentVariable("MOON_HOME").orNull
        ?: throw GradleException("checkJniContract requires MOON_HOME")
      commandLine(
        "python3", File(repositoryRoot, "scripts/check-jni-contract.py").absolutePath,
        "--class-dir", compileDebugKotlin.flatMap { it.destinationDirectory }.get().asFile.absolutePath,
        "--ndk", ndk, "--moon-home", moonHome,
      )
    }
  }
}
