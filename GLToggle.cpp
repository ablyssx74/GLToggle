/*
 * Copyright 2026, ablyss GLToggle@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */


#include <Application.h>
#include <Window.h>
#include <View.h>
#include <Button.h>
#include <StatusBar.h>
#include <StringView.h>
#include <GroupView.h>
#include <String.h>
#include <LayoutBuilder.h>
#include <CheckBox.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Directory.h>
#include <vector>
#include <set>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <Alert.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <thread>
#include <string>
#include <Notification.h>
#include <curl/curl.h>



namespace AppInfo {
	static const char* const APP_NAME = "GLToggle";
    // (the version comes from the VERSION file: the Makefile passes it in)
#ifndef APP_LOCAL_VERSION
#error "APP_LOCAL_VERSION is not set: build with the Makefile, which takes it from the VERSION file"
#endif
    static const char* const VERSION_STRING = "v" APP_LOCAL_VERSION;

}

// =============================================================================
// Update Checker
// =============================================================================
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static int32 BackgroundUpdateChecker(void* data) {
    snooze(5000000);

    printf("[UpdateChecker] Asynchronous curl update checker running...\n");

    const char* targetUrl = "https://raw.githubusercontent.com/ablyssx74/GLToggle/refs/heads/main/VERSION";

    std::string responseBuffer;

    CURL* curl = curl_easy_init();
    if (curl != nullptr) {
        curl_easy_setopt(curl, CURLOPT_URL, targetUrl);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "GLToggle-UpdateChecker/1.0");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }

    BString remoteVersionStr = responseBuffer.c_str();

    remoteVersionStr.Trim();
     printf("[UpdateChecker] Raw text received from GitHub: '%s'\n", remoteVersionStr.String());

    if (remoteVersionStr.Length() > 0) {
    	BString currentAppStr = AppInfo::APP_NAME;
        BString currentVersionStr = AppInfo::VERSION_STRING;
        printf("[UpdateChecker] Local AppInfo text before cleaning: '%s'\n", currentVersionStr.String());

        // The first number of the form X.Y.Z in a text: "Haiku v1.0.4 (x)", "v1.0.4" and a plain "1.0.4" all work. (The old
        // pattern needed a letter in front of the number, so a version file holding "1.0.4" was read as 0.0.0.)
        auto parseVersion = [](const char* text, int32& major, int32& minor, int32& revision) {
            for (const char* p = text; *p != '\0'; p++) {
                if (*p < '0' || *p > '9') continue;
                int a = 0, b = 0, c = 0;
                if (sscanf(p, "%d.%d.%d", &a, &b, &c) == 3) {
                    major = a; minor = b; revision = c;
                    return;
                }
                while (p[1] >= '0' && p[1] <= '9') p++;
            }
        };
        int32 curMajor = 0, curMinor = 0, curRevision = 0;
        int32 remMajor = 0, remMinor = 0, remRevision = 0;


        parseVersion(currentVersionStr.String(), curMajor, curMinor, curRevision);

        parseVersion(remoteVersionStr.String(), remMajor, remMinor, remRevision);

        
            printf("[UpdateChecker] Cleaned local target string: '%d.%d.%d'\n", curMajor, curMinor, curRevision);
        

        int32 currentFlattened = (curMajor * 10000) + (curMinor * 100) + curRevision;
        int32 remoteFlattened  = (remMajor * 10000) + (remMinor * 100) + remRevision;

      
            printf("[UpdateChecker] Calculated values for math match -> Local: %d | Remote: %d\n", 
                   (int)currentFlattened, (int)remoteFlattened);
        


			if (remoteFlattened > currentFlattened) {
			    printf("[UpdateChecker] Update found! Sending notification...\n");
			
			    BNotification updateAlert(B_INFORMATION_NOTIFICATION);
			    updateAlert.SetGroup(currentAppStr);
			    updateAlert.SetTitle("Update Available");
			    
			    BString alertContent;
			    // Added spaces around currentAppStr so it reads: "of GLToggle is available!"
			    alertContent << "A newer version of " << currentAppStr << " is available! (" << remoteVersionStr << ")";
			    updateAlert.SetContent(alertContent.String());
			    
			    updateAlert.Send();
			    printf("[UpdateChecker] Toast notification sent successfully.\n");
			} else {
			    printf("[UpdateChecker] Client binary is up to date.\n");
			}

		    } else {
		        printf("[UpdateChecker] CRITICAL ERR: Raw text data read from pipe buffer was empty!\n");
		    }
    
    return B_OK;
}



const char* kAppSignature = "application/x-vnd.gltoggle"; 

enum {
    MSG_UPDATE_PROGRESS = 'uprg',
    MSG_TASK_FINISHED   = 'tfsh',
    MSG_INSTALL_BASE    = 'inst',
    MSG_INSTALL_NEBULA  = 'ineb',
    MSG_DEV_TOGGLE      = 'devt'
};

// =============================================================================
// Dev packages that are put back when the Mesa libs are restored
// =============================================================================
// Nebula (libglvnd) replaces Mesa, and the development packages that depend on Mesa
// go with it. Each one has a check box; the choice is kept in the settings file
// (~/config/settings/GLToggle/settings, one "package on|off" line each) so the next
// start of the program has the same boxes. Packages that Nebula removes from the
// system are added to the list when it is installed.
struct DevPackage {
    std::string name;
    bool        on;
};

static std::vector<DevPackage> gDevPackages;

static BString SettingsFilePath()
{
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK) return BString();
    path.Append("GLToggle");
    create_directory(path.Path(), 0755);
    path.Append("settings");
    return BString(path.Path());
}

static void AddDevPackage(const std::string& name, bool on)
{
    for (const DevPackage& d : gDevPackages) if (d.name == name) return;
    gDevPackages.push_back({name, on});
}

static void LoadDevPackages()
{
    gDevPackages.clear();
    // the ones that were always installed with the Mesa libs
    AddDevPackage("glu_devel", true);
    AddDevPackage("glew_devel", true);
    AddDevPackage("mpv_devel", true);

    BString path = SettingsFilePath();
    if (path.Length() == 0) return;
    std::ifstream in(path.String());
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string name, state;
        if (!(ls >> name >> state)) continue;
        bool on = (state != "off");
        bool found = false;
        for (DevPackage& d : gDevPackages)
            if (d.name == name) { d.on = on; found = true; }
        if (!found) gDevPackages.push_back({name, on});
    }
}

static void SaveDevPackages()
{
    BString path = SettingsFilePath();
    if (path.Length() == 0) return;
    std::ofstream out(path.String(), std::ios::trunc);
    for (const DevPackage& d : gDevPackages)
        out << d.name << ' ' << (d.on ? "on" : "off") << '\n';
}

// the names of the installed development packages (the ones named *_devel)
static std::set<std::string> InstalledDevPackages()
{
    std::set<std::string> names;
    FILE* f = popen("ls /packages 2>/dev/null", "r");
    if (f == nullptr) return names;
    char buf[512];
    while (fgets(buf, sizeof(buf), f) != nullptr) {
        std::string n(buf);
        while (!n.empty() && (n.back() == '\n' || n.back() == '\r')) n.pop_back();
        // "mpv_devel-0.41.0-4" -> "mpv_devel": the name ends before the first "-<digit>"
        for (size_t i = 0; i + 1 < n.size(); i++) {
            if (n[i] == '-' && n[i + 1] >= '0' && n[i + 1] <= '9') { n.resize(i); break; }
        }
        if (n.size() > 6 && n.compare(n.size() - 6, 6, "_devel") == 0) names.insert(n);
    }
    pclose(f);
    return names;
}

const char* kUrlGlvnd   = "https://github.com/X547/nvidia-haiku/releases/download/v0.0.1/libglvnd-1.7.0-4-x86_64.hpkg";
const char* kUrlNebula2 = "https://github.com/ablyssx74/nvidia-haiku/releases/download/v9/nebula-0.0.2-22-x86_64.hpkg";

class DriverToggleWindow : public BWindow {
public:
    DriverToggleWindow() : BWindow(BRect(0, 0, 540, 160), "Mesa / Nebula Stack Selector", 
                                   B_DOCUMENT_WINDOW, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS) {
        
        BView* panel = new BView("main_panel", B_WILL_DRAW);
        panel->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

        fStatusLabel = new BStringView("status_label", "Checking driver state...");
        fStatusLabel->SetFont(be_bold_font);        
        fStatusLabel->SetAlignment(B_ALIGN_CENTER);
        fStatusBar = new BStatusBar("progress_bar", "Current Step:", "Ready");
        fStatusBar->SetMaxValue(100.0);
        fStatusBar->Hide();

        fActionBtn = new BButton("action_btn", "Evaluating State...", new BMessage(MSG_INSTALL_NEBULA));
        LoadDevPackages();
        fDevBox = new BGroupView(B_VERTICAL, 0);
        fStatusLabel->SetExplicitMinSize(BSize(540.0, B_SIZE_UNSET));

        BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
            .AddGroup(B_VERTICAL, B_USE_ITEM_SPACING)
                .SetInsets(B_USE_WINDOW_INSETS)
                .Add(fStatusLabel)
                .Add(fStatusBar)
                .Add(fDevLabel = new BStringView("dev_label", "Put these development packages back with the Mesa libs:"))
                .Add(fDevBox)
                .AddGlue()
                .AddGroup(B_HORIZONTAL, B_USE_ITEM_SPACING)
                    .AddGlue()
                    .Add(fActionBtn) 
                    .AddGlue()
                .End()
            .End();

        // =========================================================================
        // AUTOMATED BACKGROUND UPDATE CHECKER THREAD INITIALIZATION
        // =========================================================================
        thread_id updateThread = spawn_thread(BackgroundUpdateChecker, "UpdateCheckerThread", B_NORMAL_PRIORITY, this);
        if (updateThread >= 0) {
            resume_thread(updateThread);
        }
        // =========================================================================

        CenterOnScreen();
        _RebuildDevBoxes();
        _UpdateDriverStatus();
    }


    ~DriverToggleWindow() {
        system("rm -f /tmp/libglvnd-1.7.0-4-x86_64.hpkg /tmp/nebula-0.0.2-1.x86_64.hpkg");
    }


    bool QuitRequested() override {
        be_app->PostMessage(B_QUIT_REQUESTED);
        return true;
    }

    void MessageReceived(BMessage* message) override {
        switch (message->what) {
            case MSG_UPDATE_PROGRESS: {
                float delta;
                const char* text;
                if (message->FindFloat("delta", &delta) == B_OK &&
                    message->FindString("text", &text) == B_OK) {
                    fStatusBar->Update(delta, text);
                }
                break;
            }

            case MSG_DEV_TOGGLE: {
                const char* name;
                int32 value;
                if (message->FindString("pkg", &name) == B_OK && message->FindInt32("be:value", &value) == B_OK) {
                    for (DevPackage& d : gDevPackages)
                        if (d.name == name) d.on = (value == B_CONTROL_ON);
                    SaveDevPackages();
                }
                break;
            }

            case MSG_TASK_FINISHED: {
                const char* completionMsg = message->FindString("msg");
                // development packages that Nebula took away: they get a check box
                const char* removedName;
                bool added = false;
                for (int32 i = 0; message->FindString("removed", i, &removedName) == B_OK; i++) {
                    AddDevPackage(removedName, true);
                    added = true;
                }
                if (added) {
                    SaveDevPackages();
                    _RebuildDevBoxes();
                }
                
                fStatusBar->Update(100.0 - fStatusBar->CurrentValue(), NULL, completionMsg);
                snooze(500000); 
                fStatusBar->Hide();
                
                fActionBtn->SetEnabled(true);
                _RebuildDevBoxes();
                _UpdateDriverStatus();

                BNotification notification(B_INFORMATION_NOTIFICATION);
                notification.SetTitle("Driver Toggler");
                notification.SetContent(completionMsg);
                
                notification.Send();
                break;
            }


            case MSG_INSTALL_BASE: {
                _LockControls("Initiating Driver Uninstallation Sequence...");
                std::vector<std::string> chosen;
                for (const DevPackage& d : gDevPackages) if (d.on) chosen.push_back(d.name);
                std::thread worker(&DriverToggleWindow::_WorkerBaseMesaStack, this, chosen);
                worker.detach();
                break;
            }

            case MSG_INSTALL_NEBULA: {
                _LockControls("Preparing Official Release Downloads...");
                std::thread worker(&DriverToggleWindow::_WorkerNebulaStack, this);
                worker.detach();
                break;
            }

            default:
                BWindow::MessageReceived(message);
                break;
        }
    }

private:
    // one check box per development package, two on a line
    void _RebuildDevBoxes() {
        while (fDevBox->CountChildren() > 0) {
            BView* child = fDevBox->ChildAt(0);
            fDevBox->RemoveChild(child);
            delete child;
        }
        BGroupView* row = nullptr;
        int column = 0;
        for (const DevPackage& d : gDevPackages) {
            if (column == 0) {
                row = new BGroupView(B_HORIZONTAL, B_USE_ITEM_SPACING);
                fDevBox->AddChild(row);
            }
            BMessage* msg = new BMessage(MSG_DEV_TOGGLE);
            msg->AddString("pkg", d.name.c_str());
            BCheckBox* box = new BCheckBox(d.name.c_str(), d.name.c_str(), msg);
            box->SetValue(d.on ? B_CONTROL_ON : B_CONTROL_OFF);
            box->SetTarget(this);
            box->SetExplicitMinSize(BSize(250, B_SIZE_UNSET));
            row->AddChild(box);
            column = (column + 1) % 2;
        }
        InvalidateLayout();
    }

    void _LockControls(const char* initialLabel) {
        fStatusBar->Reset(initialLabel);
        fStatusBar->Show();
        fActionBtn->SetEnabled(false); 
        _SetDevVisible(false);
    }

    // The development package boxes belong to "Restore Base Mesa Libs", so they
    // are only there while the Nebula stack is active; with Mesa active they would
    // only suggest that something is installed back when Nebula goes on.
    // (Hide and Show of a view count, so this is called with the state wanted.)
    void _SetDevVisible(bool visible) {
        if (visible == fDevVisible) return;
        fDevVisible = visible;
        if (visible) { fDevLabel->Show(); fDevBox->Show(); }
        else         { fDevLabel->Hide(); fDevBox->Hide(); }
    }


    void _PostProgressUpdate(float delta, const char* text) {
        BMessage msg(MSG_UPDATE_PROGRESS);
        msg.AddFloat("delta", delta);
        msg.AddString("text", text);
        PostMessage(&msg);
    }

    void _PostTaskFinished(const char* completionAlertText, bool isError = false,
                           const std::vector<std::string>& removed = std::vector<std::string>()) {
        BMessage msg(MSG_TASK_FINISHED);
        msg.AddString("msg", completionAlertText);
        for (const std::string& r : removed) msg.AddString("removed", r.c_str());
        msg.AddBool("is_error", isError); 
        PostMessage(&msg);
    }

    void _UpdateDriverStatus() {
        struct stat mesaBuffer;
        bool hasHardwareDriver = (stat("/boot/system/add-ons/opengl/egl_vendor.d/libEGL_mesa.so", &mesaBuffer) == 0);

        if (hasHardwareDriver) {
            fStatusLabel->SetText("Current State: NVIDIA Nebula Driver Stack Active");            
            fActionBtn->SetLabel("Restore Base Mesa Libs");
            fActionBtn->SetMessage(new BMessage(MSG_INSTALL_BASE));
            fActionBtn->SetEnabled(true);
            _SetDevVisible(true);
        } else {
            fStatusLabel->SetText("Current State: Base Haiku Mesa Libs Active");            
            fActionBtn->SetLabel("Install NVIDIA Nebula Driver");
            fActionBtn->SetMessage(new BMessage(MSG_INSTALL_NEBULA));
            fActionBtn->SetEnabled(true);
            _SetDevVisible(false);
        }
        
        InvalidateLayout();
    }

    void _WorkerBaseMesaStack(std::vector<std::string> devPackages) {
        _PostProgressUpdate(15.0, "Safely stripping NVIDIA Nebula modules...");
        system("pkgman uninstall -y nebula");
        
        _PostProgressUpdate(30.0, "Deploying baseline software fallback dependencies...");
        system("pkgman install -y mesa_devel mesa_swpipe mesa_lavapipe");

        // the development packages that were ticked, one at a time: a package that
        // is not there any more must not stop the others
        for (const std::string& name : devPackages) {
            BString status;
            status.SetToFormat("Installing %s...", name.c_str());
            _PostProgressUpdate(0.0, status.String());
            BString cmd;
            cmd.SetToFormat("pkgman install -y %s", name.c_str());
            system(cmd.String());
        }
        
        _PostProgressUpdate(30.0, "Removing libglvnd infrastructure...");
        system("pkgman uninstall -y libglvnd");
        
        _PostProgressUpdate(20.0, "Finalizing baseline software restore...");
        _PostTaskFinished("Successfully reverted to Base Haiku Mesa Stack.");
    }


    bool _VerifyFileHash(const char* filePath, const char* expectedHash) {
        BString verifyCmd;
        verifyCmd.SetToFormat("echo \"%s  %s\" | sha256sum -c --status", expectedHash, filePath);
        
        int status = system(verifyCmd.String());
        return (status == 0);
    }

    void _WorkerNebulaStack() {
        system("mkdir -p /tmp/nebula_setup");
        std::set<std::string> develBefore = InstalledDevPackages();

        const char* downloadsGlvnd = "/boot/home/Downloads/libglvnd-1.7.0-4-x86_64.hpkg";
        const char* downloadsNebula = "/boot/home/Downloads/nebula-0.0.2-22-x86_64.hpkg";

        const char* tmpGlvnd = "/tmp/libglvnd-1.7.0-4-x86_64.hpkg";
        const char* tmpNebula = "/tmp/nebula-0.0.2-1.x86_64.hpkg";

        const char* hashGlvnd = "0a4dba881e0a3c3f60a82cef84b7668a919569d8d880ee9011855b8539cb355b";
        const char* hashNebula = "8eeb247bbbcdb1064cfb0687caed0152828bdddbce8c619a1f6a99d7de016303";

        BString activeGlvndPath;
        BString activeNebulaPath;

        // --- libglvnd verification & retrieval ---
        struct stat glvndStat;
        if (stat(downloadsGlvnd, &glvndStat) == 0 && _VerifyFileHash(downloadsGlvnd, hashGlvnd)) {
            _PostProgressUpdate(10.0, "Found cached libglvnd framework package in Downloads. Skipping download.");
            activeGlvndPath = downloadsGlvnd;
            snooze(300000);
        } else if (stat(tmpGlvnd, &glvndStat) == 0 && _VerifyFileHash(tmpGlvnd, hashGlvnd)) {
            _PostProgressUpdate(10.0, "Found cached libglvnd framework package in /tmp. Skipping download.");
            activeGlvndPath = tmpGlvnd;
            snooze(300000);
        } else {
            unlink(tmpGlvnd);
            _PostProgressUpdate(10.0, "Downloading official libglvnd framework package...");
            BString downloadGlvndCmd;
            downloadGlvndCmd.SetToFormat("curl -L --fail --retry 3 --connect-timeout 15 -o %s %s", tmpGlvnd, kUrlGlvnd);
            
            if (system(downloadGlvndCmd.String()) != 0) {
                _PostTaskFinished("Error: libglvnd download failed. Check network link.", true);
                return;
            }
            activeGlvndPath = tmpGlvnd;
        }

        // --- nebula verification & retrieval ---
        struct stat nebulaStat;
        if (stat(downloadsNebula, &nebulaStat) == 0 && _VerifyFileHash(downloadsNebula, hashNebula)) {
            _PostProgressUpdate(25.0, "Found cached NVIDIA Nebula kernel package in Downloads. Skipping download.");
            activeNebulaPath = downloadsNebula;
            snooze(300000);
        } else if (stat(tmpNebula, &nebulaStat) == 0 && _VerifyFileHash(tmpNebula, hashNebula)) {
            _PostProgressUpdate(25.0, "Found cached NVIDIA Nebula kernel package in /tmp. Skipping download.");
            activeNebulaPath = tmpNebula;
            snooze(300000);
        } else {
            unlink(tmpNebula);
            _PostProgressUpdate(25.0, "Downloading official NVIDIA Nebula kernel package...");
            BString downloadNebulaCmd;
            downloadNebulaCmd.SetToFormat("curl -L --fail --retry 3 --connect-timeout 15 -o %s %s", tmpNebula, kUrlNebula2);
            
            if (system(downloadNebulaCmd.String()) != 0) {
                _PostTaskFinished("Error: Nebula download failed. Check network link.", true);
                return;
            }
            activeNebulaPath = tmpNebula;
        }

        _PostProgressUpdate(15.0, "Verifying cryptographic signatures...");
        if (!_VerifyFileHash(activeGlvndPath.String(), hashGlvnd) || !_VerifyFileHash(activeNebulaPath.String(), hashNebula)) {
            system("rm -f /tmp/libglvnd-1.7.0-4-x86_64.hpkg /tmp/nebula-0.0.2-1.x86_64.hpkg");
            _PostTaskFinished("Error: Download verification failed. Package hash is corrupt.", true);
            return;
        }

        _PostProgressUpdate(15.0, "Deploying verified libglvnd framework package...");
        BString installGlvndCmd;
        installGlvndCmd.SetToFormat("pkgman install -y %s", activeGlvndPath.String());
        system(installGlvndCmd.String());

        _PostProgressUpdate(10.0, "Purging conflicting software Mesa frameworks...");
        system("pkgman uninstall -y mesa mesa_lavapipe");    
        
        _PostProgressUpdate(10.0, "Deploying verified NVIDIA Nebula driver stack...");
        BString installNebulaCmd;
        installNebulaCmd.SetToFormat("pkgman install -y %s", activeNebulaPath.String());
        system(installNebulaCmd.String());
        
        // the development packages that are gone now get a check box
        std::set<std::string> develAfter = InstalledDevPackages();
        std::vector<std::string> removed;
        for (const std::string& n : develBefore)
            if (develAfter.find(n) == develAfter.end()) removed.push_back(n);
        _PostTaskFinished("Successfully initialized NVIDIA Nebula graphics drivers from official repository.", false, removed);
    }



    BStringView*     fStatusLabel;
    BStatusBar*      fStatusBar;
    BButton*         fActionBtn; 
    BStringView*     fDevLabel;
    BGroupView*      fDevBox;
    bool             fDevVisible = true;
};

class DriverToggleApp : public BApplication {
public:
    DriverToggleApp() : BApplication(kAppSignature) {}
    void ReadyToRun() override {
        DriverToggleWindow* window = new DriverToggleWindow();
        window->Show();
    }
};

int main() {
    // libcurl's global init is not thread-safe against other concurrently
    // running threads, so it is performed once, up front, here rather than
    // relying on an implicit lazy global init that could race with the
    // background update-checker thread (or other background threads) later.
    curl_global_init(CURL_GLOBAL_DEFAULT);

    DriverToggleApp app;
    app.Run();

    curl_global_cleanup();
    return 0;
}
