// Customgram updater for Windows (.NET Framework 4.5+).
// Compiled with the system C# compiler by BuildUpdater.cmd.
// It checks GitHub Releases, verifies the release ZIP's SHA-256,
// updates ONLY Customgram.exe and modules/, and preserves data/.
// This software is provided as a starter implementation, not a code-signed release updater.
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;
using System.Windows.Forms;

internal static class CustomgramUpdater {
    const string ZipAssetName = "Customgram-Windows.zip";
    const string HashAssetName = "Customgram-Windows.zip.sha256";
    const long MaxZipBytes = 750L * 1024 * 1024;
    const long MaxUnpackedBytes = 1500L * 1024 * 1024;
    static readonly StringComparison IgnoreCase = StringComparison.OrdinalIgnoreCase;

    [STAThread]
    static int Main(string[] args) {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        bool checkOnly = args.Length > 0 && args[0] == "--check";
        bool applyAfterExit = args.Length == 2 && args[0] == "--apply-after-exit";
        bool launch = (args.Length > 0 && args[0] == "--launch") || applyAfterExit;
        bool manual = !launch && !checkOnly;
        string root = AppDomain.CurrentDomain.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar);
        string program = Path.Combine(root, "Customgram.exe");
        try {
            if (!File.Exists(program)) {
                if (!checkOnly) Error("Рядом с Updater.exe не найден Customgram.exe.");
                return 2;
            }
            string repo = ReadRepo(root);
            if (checkOnly) {
                if (repo == null) return 2;
                try {
                    Version latest;
                    Version current;
                    GetVersionPair(root, repo, out latest, out current);
                    return latest.CompareTo(current) > 0 ? 10 : 0;
                } catch {
                    return 2; // Offline, bad release or invalid config: no UI.
                }
            }
            if (applyAfterExit) {
                int pid;
                if (!int.TryParse(args[1], out pid) || pid <= 0)
                    throw new InvalidOperationException("Неверный идентификатор процесса.");
                WaitForCustomgramToExit(pid, program);
            }
            // --launch now starts Customgram immediately. The in-app UI
            // performs its own silent periodic checks through --check.
            if (repo != null && (!launch || applyAfterExit)) {
                try {
                    CheckAndApply(root, program, repo, manual);
                } catch (Exception e) {
                    if (manual || applyAfterExit)
                        Error("Не удалось проверить/применить обновление:\n\n" + e.Message);
                    // Keep the old app usable if the update fails.
                }
            } else if (manual) {
                Info("Настрой GitHub-репозиторий в github-repo.txt (пример: owner/repo).\n"
                   + "Пока интернет-проверка отключена.");
            }
        } catch (Exception e) {
            if (!checkOnly) Error("Ошибка Updater.exe:\n\n" + e.Message);
            if (!launch) return 1;
        }
        if (launch) {
            try {
                string data = Path.Combine(root, "data");
                Directory.CreateDirectory(data);
                Process.Start(new ProcessStartInfo {
                    FileName = program,
                    WorkingDirectory = root,
                    Arguments = "-many -workdir \"" + data + "\" -noupdate",
                    UseShellExecute = false
                });
            } catch (Exception e) {
                Error("Customgram не запустился:\n\n" + e.Message);
                return 3;
            }
        }
        return 0;
    }

    static string ReadRepo(string root) {
        string path = Path.Combine(root, "github-repo.txt");
        if (!File.Exists(path)) return null;
        string repo = File.ReadAllText(path, Encoding.UTF8).Trim();
        if (repo.StartsWith("#") || repo.Contains("OWNER") || repo.Contains("REPO")) return null;
        // Only repo names, not URLs. No arbitrary external update servers.
        return Regex.IsMatch(repo, @"\A[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+\z") ? repo : null;
    }

    static void CheckAndApply(string root, string program, string repo, bool manual) {
        Version latest;
        Version current;
        Dictionary<string, object> release = GetVersionPair(root, repo, out latest, out current);
        string versionFile = Path.Combine(root, "version.txt");
        if (latest.CompareTo(current) <= 0) {
            if (manual) Info("Установлена актуальная версия Customgram V" + current + ".");
            return;
        }
        object[] assets = release["assets"] as object[];
        if (assets == null) throw new InvalidOperationException("В GitHub Release нет assets.");
        string zipUrl = GetAssetUrl(assets, ZipAssetName);
        string shaUrl = GetAssetUrl(assets, HashAssetName);
        if (zipUrl == null || shaUrl == null) {
            throw new InvalidOperationException("В релизе нужны " + ZipAssetName
                + " и " + HashAssetName + ".");
        }
        if (RunningHere(program)) {
            if (manual) Info("Закрой Customgram полностью и снова запусти обновление.");
            return;
        }
        DialogResult answer = MessageBox.Show(
            "Доступна версия Customgram V" + latest + ".\n"
            + "Текущая версия: V" + current + ".\n\n"
            + "Скачать и установить обновление?\n\n"
            + "Папка data и авторизация останутся нетронутыми.",
            "Customgram Update", MessageBoxButtons.YesNo, MessageBoxIcon.Question);
        if (answer != DialogResult.Yes) return;
        string tempRoot = Path.Combine(Path.GetTempPath(), "CustomgramUpdate-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tempRoot);
        try {
            string archivePath = Path.Combine(tempRoot, ZipAssetName);
            string extractPath = Path.Combine(tempRoot, "payload");
            string shaText;
            using (WebClient client = NewClient()) shaText = client.DownloadString(shaUrl);
            Match match = Regex.Match(shaText, @"\b[A-Fa-f0-9]{64}\b");
            if (!match.Success) throw new InvalidOperationException("Неверный формат SHA-256 файла.");
            DownloadWithProgress(zipUrl, archivePath, "Customgram V" + latest);
            if (new FileInfo(archivePath).Length > MaxZipBytes)
                throw new InvalidOperationException("Архив слишком большой.");
            string actual = Sha256(archivePath);
            if (!actual.Equals(match.Value, IgnoreCase)) {
                throw new InvalidOperationException("SHA-256 не совпадает. Обновление отменено!");
            }
            ExtractValidatedZip(archivePath, extractPath);
            ApplyUpdate(root, program, extractPath, versionFile, latest.ToString());
            Info("Customgram обновлён до V" + latest + ".\nАвторизация сохранена.");
        } finally {
            try { Directory.Delete(tempRoot, true); } catch { /* harmless temporary files */ }
        }
    }

    // The GitHub /releases/latest endpoint skips prereleases. Use the releases
    // collection so testing versions (e.g. v13.0.2 marked prerelease) work.
    static Dictionary<string, object> GetVersionPair(
            string root, string repo, out Version latest, out Version current) {
        ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;
        current = ParseVersion(File.Exists(Path.Combine(root, "version.txt"))
            ? File.ReadAllText(Path.Combine(root, "version.txt")).Trim() : "13.0.1");
        string json;
        using (WebClient client = NewClient()) {
            json = client.DownloadString(
                "https://api.github.com/repos/" + repo + "/releases?per_page=30");
        }
        object[] releases = new JavaScriptSerializer { MaxJsonLength = 16 * 1024 * 1024 }
            .DeserializeObject(json) as object[];
        if (releases == null) throw new InvalidOperationException("Некорректный ответ GitHub Releases.");
        Dictionary<string, object> newest = null;
        latest = new Version(0, 0, 0);
        foreach (object item in releases) {
            Dictionary<string, object> release = item as Dictionary<string, object>;
            if (release == null || GetBool(release, "draft")) continue;
            Version version;
            try { version = ParseVersion(GetString(release, "tag_name")); }
            catch { continue; }
            object listedAssets;
            if (!release.TryGetValue("assets", out listedAssets)) continue;
            object[] candidateAssets = listedAssets as object[];
            if (candidateAssets == null) continue;
            if (GetAssetUrl(candidateAssets, ZipAssetName) == null ||
                GetAssetUrl(candidateAssets, HashAssetName) == null) continue;
            if (version.CompareTo(latest) > 0) {
                latest = version;
                newest = release;
            }
        }
        if (newest == null) throw new InvalidOperationException("Нет опубликованных релизов Customgram.");
        return newest;
    }

    static bool GetBool(Dictionary<string, object> data, string key) {
        object result;
        return data.TryGetValue(key, out result) && result is bool && (bool)result;
    }

    static void WaitForCustomgramToExit(int pid, string program) {
        try {
            using (Process p = Process.GetProcessById(pid)) {
                // Never wait on an unrelated process if Windows recycled the PID.
                if (!string.Equals(Path.GetFullPath(p.MainModule.FileName),
                    Path.GetFullPath(program), IgnoreCase)) return;
                if (!p.WaitForExit(90000))
                    throw new TimeoutException("Customgram не закрылся за 90 секунд.");
            }
        } catch (ArgumentException) {
            // Already exited.
        }
    }

    static WebClient NewClient() {
        WebClient result = new WebClient();
        result.Headers[HttpRequestHeader.UserAgent] = "Customgram-Updater/1.0";
        result.Headers[HttpRequestHeader.Accept] = "application/vnd.github+json";
        return result;
    }

    static Dictionary<string, object> ParseObject(string json) {
        Dictionary<string, object> parsed = new JavaScriptSerializer()
            .DeserializeObject(json) as Dictionary<string, object>;
        if (parsed == null) throw new InvalidOperationException("Некорректный ответ GitHub.");
        return parsed;
    }

    static string GetString(Dictionary<string, object> data, string key) {
        object value;
        return data.TryGetValue(key, out value) ? Convert.ToString(value) : "";
    }

    static Version ParseVersion(string value) {
        Match match = Regex.Match(value.Trim(), @"\Av?(\d+)\.(\d+)\.(\d+)\z", RegexOptions.IgnoreCase);
        if (!match.Success) throw new InvalidOperationException("Версия должна быть вида v13.0.2: " + value);
        return new Version(int.Parse(match.Groups[1].Value), int.Parse(match.Groups[2].Value), int.Parse(match.Groups[3].Value));
    }

    static string GetAssetUrl(object[] assets, string wantedName) {
        foreach (object item in assets) {
            Dictionary<string, object> asset = item as Dictionary<string, object>;
            if (asset == null || GetString(asset, "name") != wantedName) continue;
            string value = GetString(asset, "browser_download_url");
            Uri uri;
            if (!Uri.TryCreate(value, UriKind.Absolute, out uri) || uri.Scheme != "https"
                    || !uri.Host.Equals("github.com", IgnoreCase)) {
                throw new InvalidOperationException("Недопустимый адрес обновления.");
            }
            return value;
        }
        return null;
    }

    static bool RunningHere(string exe) {
        foreach (Process p in Process.GetProcessesByName("Customgram")) {
            try {
                if (string.Equals(Path.GetFullPath(p.MainModule.FileName),
                    Path.GetFullPath(exe), StringComparison.OrdinalIgnoreCase)) return true;
            } catch { /* Windows may deny querying some processes */ }
            finally { p.Dispose(); }
        }
        return false;
    }

    static string Sha256(string file) {
        using (SHA256 alg = SHA256.Create())
        using (FileStream stream = File.OpenRead(file)) {
            byte[] result = alg.ComputeHash(stream);
            return BitConverter.ToString(result).Replace("-", "").ToLowerInvariant();
        }
    }

    static void DownloadWithProgress(string url, string path, string version) {
        Uri uri = new Uri(url);
        using (WebClient client = NewClient())
        using (Form window = new Form()) {
            window.Text = "Customgram Updater";
            window.Width = 420;
            window.Height = 160;
            window.FormBorderStyle = FormBorderStyle.FixedDialog;
            window.MaximizeBox = false;
            window.MinimizeBox = false;
            window.StartPosition = FormStartPosition.CenterScreen;
            window.ControlBox = false;
            Label label = new Label { Left = 16, Top = 15, Width = 378,
                Text = "Загружается " + version + "..." };
            ProgressBar bar = new ProgressBar { Left = 16, Top = 47, Width = 378,
                Height = 22, Minimum = 0, Maximum = 100, Style = ProgressBarStyle.Marquee };
            Button cancel = new Button { Text = "Отмена", Left = 304, Top = 79, Width = 90 };
            window.Controls.Add(label);
            window.Controls.Add(bar);
            window.Controls.Add(cancel);
            Exception failure = null;
            bool cancelled = false;
            cancel.Click += delegate { cancelled = true; client.CancelAsync(); cancel.Enabled = false; };
            client.DownloadProgressChanged += delegate(object sender, DownloadProgressChangedEventArgs e) {
                if (e.TotalBytesToReceive > 0) {
                    bar.Style = ProgressBarStyle.Continuous;
                    bar.Value = Math.Max(0, Math.Min(100, e.ProgressPercentage));
                    label.Text = "Загружается " + version + ": " + e.ProgressPercentage + "%";
                }
            };
            client.DownloadFileCompleted += delegate(object sender, AsyncCompletedEventArgs e) {
                failure = e.Error;
                cancelled = cancelled || e.Cancelled;
                window.Close();
            };
            window.Shown += delegate { client.DownloadFileAsync(uri, path); };
            Application.Run(window);
            if (cancelled) throw new OperationCanceledException("Загрузка отменена.");
            if (failure != null) throw new IOException("Ошибка загрузки: " + failure.Message, failure);
            if (!File.Exists(path)) throw new IOException("Файл обновления не скачан.");
        }
    }

    static void ExtractValidatedZip(string zipPath, string destination) {
        Directory.CreateDirectory(destination);
        bool hasExe = false;
        long unpacked = 0;
        using (ZipArchive zip = ZipFile.OpenRead(zipPath)) {
            foreach (ZipArchiveEntry entry in zip.Entries) {
                string name = entry.FullName.Replace('\\', '/');
                if (name.StartsWith("/", StringComparison.Ordinal) || name.IndexOf(':') >= 0)
                    throw new InvalidOperationException("Запрещённый путь внутри архива.");
                foreach (string segment in name.Split('/')) {
                    if (segment == ".." || segment == ".")
                        throw new InvalidOperationException("Запрещённый путь ../ внутри архива.");
                }
                if (name.Length == 0) continue;
                bool folder = name.EndsWith("/", StringComparison.Ordinal);
                if (!name.Equals("Customgram.exe", IgnoreCase)
                    && !name.Equals("Updater.exe", IgnoreCase)
                    && !name.Equals("github-repo.txt", IgnoreCase)
                    && !name.Equals("version.txt", IgnoreCase)
                    && !name.Equals("modules/", IgnoreCase)
                    && !name.StartsWith("modules/", IgnoreCase)) {
                    throw new InvalidOperationException("Архив содержит посторонний файл: " + name);
                }
                // Unix symlink entries must never be followed or extracted.
                if (((entry.ExternalAttributes >> 16) & 0xF000) == 0xA000)
                    throw new InvalidOperationException("Символические ссылки в ZIP запрещены.");
                if (entry.Length < 0 || unpacked > MaxUnpackedBytes - entry.Length)
                    throw new InvalidOperationException("Слишком большой распакованный архив.");
                unpacked += entry.Length;
                string target = Path.GetFullPath(Path.Combine(destination, name.Replace('/', Path.DirectorySeparatorChar)));
                string prefix = Path.GetFullPath(destination) + Path.DirectorySeparatorChar;
                if (!target.StartsWith(prefix, IgnoreCase))
                    throw new InvalidOperationException("Некорректный путь файла в ZIP.");
                if (folder) {
                    Directory.CreateDirectory(target);
                } else {
                    Directory.CreateDirectory(Path.GetDirectoryName(target));
                    using (Stream input = entry.Open())
                    using (FileStream output = File.Create(target)) input.CopyTo(output);
                }
                if (name.Equals("Customgram.exe", IgnoreCase)) hasExe = true;
            }
        }
        if (!hasExe) throw new InvalidOperationException("В ZIP отсутствует Customgram.exe в корне.");
        string exe = Path.Combine(destination, "Customgram.exe");
        using (FileStream fs = File.OpenRead(exe)) {
            if (fs.Length < 102400 || fs.ReadByte() != 'M' || fs.ReadByte() != 'Z')
                throw new InvalidOperationException("Customgram.exe в архиве не является корректным PE-файлом.");
        }
    }

    static void ApplyUpdate(string root, string program, string staged, string versionFile, string version) {
        string newExe = Path.Combine(staged, "Customgram.exe");
        string modules = Path.Combine(root, "modules");
        string newModules = Path.Combine(staged, "modules");
        string backupExe = Path.Combine(root, "Customgram.exe.previous");
        string backupModules = Path.Combine(root, "modules.previous");
        bool modulesChanged = false;
        bool exeChanged = false;
        // Remove only the previously created modules backup after staging and verification.
        if (Directory.Exists(backupModules)) Directory.Delete(backupModules, true);
        File.Copy(program, backupExe, true);
        try {
            if (Directory.Exists(newModules)) {
                if (Directory.Exists(modules)) Directory.Move(modules, backupModules);
                Directory.Move(newModules, modules);
                modulesChanged = true;
            }
            File.Copy(newExe, program, true);
            exeChanged = true;
            File.WriteAllText(versionFile, version + Environment.NewLine, Encoding.UTF8);
            // Keep previous EXE for manual recovery if the new build won't launch.
            // Keep modules.previous as backup when it exists.
        } catch {
            if (exeChanged || File.Exists(backupExe)) {
                try { File.Copy(backupExe, program, true); } catch { }
            }
            if (modulesChanged) {
                try {
                    if (Directory.Exists(modules)) Directory.Delete(modules, true);
                    if (Directory.Exists(backupModules)) Directory.Move(backupModules, modules);
                } catch { }
            } else if (Directory.Exists(backupModules) && !Directory.Exists(modules)) {
                try { Directory.Move(backupModules, modules); } catch { }
            }
            throw;
        }
    }

    static void Info(string text) {
        MessageBox.Show(text, "Customgram Updater", MessageBoxButtons.OK, MessageBoxIcon.Information);
    }

    static void Error(string text) {
        MessageBox.Show(text, "Customgram Updater", MessageBoxButtons.OK, MessageBoxIcon.Error);
    }
}
