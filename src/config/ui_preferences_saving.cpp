#include "stdafx.h"

#pragma warning(push, 0)
#include <ShObjIdl_core.h>
#include "resource.h"
#include "foobar2000/helpers/atl-misc.h"
#pragma warning(pop)

#include "config/config_auto.h"
#include "logging.h"
#include "preferences.h"

static const GUID GUID_PREFERENCES_PAGE_SAVING = { 0xd5a7534, 0x9f59, 0x444c, { 0x8d, 0x6f, 0xec, 0xf3, 0x7f, 0x61, 0xfc, 0xf1 } };

static const GUID GUID_CFG_SAVE_FILENAME_FORMAT = { 0x1f7a3804, 0x7147, 0x4b64, { 0x9d, 0x51, 0x4c, 0xdd, 0x90, 0xa7, 0x6d, 0xd6 } };
static const GUID GUID_CFG_SAVE_ENABLE_AUTOSAVE = { 0xf25be2d9, 0x4442, 0x4602, { 0xa0, 0xf1, 0x81, 0xd, 0x8e, 0xab, 0x6a, 0x2 } };
static const GUID GUID_CFG_SAVE_METHOD = { 0xdf39b51c, 0xec55, 0x41aa, { 0x93, 0xd3, 0x32, 0xb6, 0xc0, 0x5d, 0x4f, 0xcc } };
static const GUID GUID_CFG_SAVE_TAG_UNTIMED = { 0x39b0bc08, 0x5c3a, 0x4359, { 0x9d, 0xdb, 0xd4, 0x90, 0x84, 0xb, 0x31, 0x88 } };
static const GUID GUID_CFG_SAVE_TAG_TIMESTAMPED = { 0x337d0d40, 0xe9da, 0x4531, { 0xb0, 0x82, 0x13, 0x24, 0x56, 0xe5, 0xc4, 0x2 } };
static const GUID GUID_CFG_SAVE_DIR_CLASS = { 0xcf49878d, 0xe2ea, 0x4682, { 0x98, 0xb, 0x8f, 0xc1, 0xf3, 0x80, 0x46, 0x7b } };
static const GUID GUID_CFG_SAVE_PATH_CUSTOM = { 0x84ac099b, 0xa00b, 0x4713, { 0x8f, 0x1c, 0x30, 0x7e, 0x31, 0xc0, 0xa1, 0xdf } };

static cfg_auto_combo_option<SaveMethod> save_method_options[] =
{
    {_T("Don't save"), SaveMethod::None},
    {_T("Save to file"), SaveMethod::LocalFile},
    {_T("Save to tag"), SaveMethod::Id3Tag},
};

static cfg_auto_combo_option<SaveDirectoryClass> save_dir_class_options[] =
{
    {_T("Save to the configuration directory"), SaveDirectoryClass::ConfigDirectory},
    {_T("Save to the same directory as the track"), SaveDirectoryClass::TrackFileDirectory},
    {_T("Save to a custom directory"), SaveDirectoryClass::Custom},
};

static cfg_auto_bool                         cfg_save_auto_save_enabled(GUID_CFG_SAVE_ENABLE_AUTOSAVE, IDC_AUTOSAVE_ENABLED_CHKBOX, true);
static cfg_auto_combo<SaveMethod, 3>         cfg_save_method(GUID_CFG_SAVE_METHOD, IDC_SAVE_METHOD_COMBO, SaveMethod::LocalFile, save_method_options);
static cfg_auto_string                       cfg_save_tag_untimed(GUID_CFG_SAVE_TAG_UNTIMED, IDC_SAVE_TAG_UNSYNCED, "UNSYNCEDLYRICS");
static cfg_auto_string                       cfg_save_tag_timestamped(GUID_CFG_SAVE_TAG_TIMESTAMPED, IDC_SAVE_TAG_SYNCED, "LYRICS");
static cfg_auto_string                       cfg_save_filename_format(GUID_CFG_SAVE_FILENAME_FORMAT, IDC_SAVE_FILENAME_FORMAT, "[%artist% - ][%title%]");
static cfg_auto_combo<SaveDirectoryClass, 3> cfg_save_dir_class(GUID_CFG_SAVE_DIR_CLASS, IDC_SAVE_DIRECTORY_CLASS, SaveDirectoryClass::ConfigDirectory, save_dir_class_options);
static cfg_auto_string                       cfg_save_path_custom(GUID_CFG_SAVE_PATH_CUSTOM, IDC_SAVE_CUSTOM_PATH, "C:\\Lyrics\\%artist%");

static cfg_auto_property* g_saving_auto_properties[] =
{
    &cfg_save_auto_save_enabled,
    &cfg_save_method,
    &cfg_save_filename_format,
    &cfg_save_tag_untimed,
    &cfg_save_tag_timestamped,
    &cfg_save_dir_class,
    &cfg_save_path_custom,
};

class titleformat_filename_filter : public titleformat_text_filter
{
    void write(const GUID& /*input_type*/, pfc::string_receiver& output, const char* data, t_size data_length) override
    {
        pfc::string8 input(data, data_length);
        input.fix_filename_chars();
        output.add_string(input.c_str(), input.length());
    }
};

bool preferences::saving::autosave_enabled()
{
    return cfg_save_auto_save_enabled.get_value();
}

SaveMethod preferences::saving::save_method()
{
    return cfg_save_method.get_value();
}

std::string preferences::saving::filename(metadb_handle_ptr track)
{
    const char* name_format_str = cfg_save_filename_format.c_str();
    titleformat_object::ptr name_format_script;
    bool name_compile_success = titleformat_compiler::get()->compile(name_format_script, name_format_str);
    if(!name_compile_success)
    {
        LOG_WARN("Failed to compile save file format: %s", name_format_str);
        return "";
    }

    pfc::string8 formatted_name;
    bool format_success = track->format_title(nullptr, formatted_name, name_format_script, nullptr);
    if(!format_success)
    {
        LOG_WARN("Failed to format save file title using format: %s", name_format_str);
        return "";
    }
    formatted_name.fix_filename_chars();

    pfc::string8 formatted_directory;
    SaveDirectoryClass dir_class = cfg_save_dir_class.get_value();
    switch(dir_class)
    {
        case SaveDirectoryClass::ConfigDirectory:
        {
            formatted_directory = core_api::get_profile_path();
            formatted_directory += "\\lyrics\\";
        } break;

        case SaveDirectoryClass::TrackFileDirectory:
        {
            const char* path = track->get_path();
            pfc::string tmp = path;
            tmp = pfc::io::path::getParent(tmp);
            formatted_directory = pfc::string8(tmp.c_str(), tmp.length());
        } break;

        case SaveDirectoryClass::Custom:
        {
            const char* path_format_str = cfg_save_path_custom.get_ptr();
            titleformat_object::ptr dir_format_script;
            bool dir_compile_success = titleformat_compiler::get()->compile(dir_format_script, path_format_str);
            if(!dir_compile_success)
            {
                LOG_WARN("Failed to compile save path format: %s", path_format_str);
                return "";
            }

            titleformat_filename_filter filter;
            bool dir_format_success = track->format_title(nullptr, formatted_directory, dir_format_script, &filter);
            if(!dir_format_success)
            {
                LOG_WARN("Failed to format save path using format: %s", path_format_str);
                return "";
            }
        } break;

        case SaveDirectoryClass::None:
        default:
            LOG_WARN("Unrecognised save path class: %d", (int)dir_class);
            return "";
    }

    pfc::string8 result = formatted_directory;
    result.add_filename(formatted_name);
    if(formatted_directory.is_empty() || formatted_name.is_empty())
    {
        LOG_WARN("Invalid save path: %s", result.c_str());
        return "";
    }

    return std::string(result.c_str(), result.length());
}

std::string_view preferences::saving::untimed_tag()
{
    return {cfg_save_tag_untimed.get_ptr(), cfg_save_tag_untimed.get_length()};
}

std::string_view preferences::saving::timestamped_tag()
{
    return {cfg_save_tag_timestamped.get_ptr(), cfg_save_tag_timestamped.get_length()};
}

class PreferencesSaving : public CDialogImpl<PreferencesSaving>, public auto_preferences_page_instance, private play_callback_impl_base
{
public:
    PreferencesSaving(preferences_page_callback::ptr callback) : auto_preferences_page_instance(callback, g_saving_auto_properties) {}

    // Dialog resource ID - Required by WTL/Create()
    enum {IDD = IDD_PREFERENCES_SAVING};

    //WTL message map
    BEGIN_MSG_MAP_EX(PreferencesSaving)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_HANDLER_EX(IDC_AUTOSAVE_ENABLED_CHKBOX, BN_CLICKED, OnUIChange)
        COMMAND_HANDLER_EX(IDC_SAVE_FILENAME_FORMAT, EN_CHANGE, OnSaveNameFormatChange)
        COMMAND_HANDLER_EX(IDC_SAVE_TAG_SYNCED, EN_CHANGE, OnUIChange)
        COMMAND_HANDLER_EX(IDC_SAVE_TAG_UNSYNCED, EN_CHANGE, OnUIChange)
        COMMAND_HANDLER_EX(IDC_SAVE_METHOD_COMBO, CBN_SELCHANGE, OnSaveMethodChange)
        COMMAND_HANDLER_EX(IDC_SAVE_DIRECTORY_CLASS, CBN_SELCHANGE, OnDirectoryClassChange)
        COMMAND_HANDLER_EX(IDC_SAVE_CUSTOM_PATH, EN_CHANGE, OnCustomPathFormatChange)
        COMMAND_HANDLER_EX(IDC_SAVE_CUSTOM_PATH_BROWSE, BN_CLICKED, OnCustomPathBrowse)
    END_MSG_MAP()

private:
    void on_playback_new_track(metadb_handle_ptr track) override;

    BOOL OnInitDialog(CWindow, LPARAM);
    void OnUIChange(UINT, int, CWindow);
    void OnSaveNameFormatChange(UINT, int, CWindow);
    void OnSaveMethodChange(UINT, int, CWindow);
    void OnDirectoryClassChange(UINT, int, CWindow);
    void OnCustomPathFormatChange(UINT, int, CWindow);
    void OnCustomPathBrowse(UINT, int, CWindow);

    void UpdateFormatPreview(int edit_id, int preview_id, bool is_path);
    void SetMethodFieldsEnabled();
    void SetCustomPathEnabled();
};

void PreferencesSaving::on_playback_new_track(metadb_handle_ptr /*track*/)
{
    UpdateFormatPreview(IDC_SAVE_FILENAME_FORMAT, IDC_SAVE_FILE_NAME_PREVIEW, false);
    SetCustomPathEnabled();
}

BOOL PreferencesSaving::OnInitDialog(CWindow, LPARAM)
{
    init_auto_preferences();
    SetMethodFieldsEnabled();
    return FALSE;
}

void PreferencesSaving::OnUIChange(UINT, int, CWindow)
{
    on_ui_interaction();
}

void PreferencesSaving::OnSaveNameFormatChange(UINT, int, CWindow)
{
    UpdateFormatPreview(IDC_SAVE_FILENAME_FORMAT, IDC_SAVE_FILE_NAME_PREVIEW, false);
    on_ui_interaction();
}

void PreferencesSaving::OnSaveMethodChange(UINT, int, CWindow)
{
    SetMethodFieldsEnabled();
    on_ui_interaction();
}

void PreferencesSaving::OnDirectoryClassChange(UINT, int, CWindow)
{
    SetCustomPathEnabled();
    on_ui_interaction();
}

void PreferencesSaving::OnCustomPathFormatChange(UINT, int, CWindow)
{
    UpdateFormatPreview(IDC_SAVE_CUSTOM_PATH, IDC_SAVE_CUSTOM_PATH_PREVIEW, true);
    on_ui_interaction();
}

void PreferencesSaving::OnCustomPathBrowse(UINT, int, CWindow)
{
    std::wstring result;
    bool success = false;
    IFileDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if(SUCCEEDED(hr))
    {
        DWORD flags;
        hr = dialog->GetOptions(&flags);
        if(SUCCEEDED(hr))
        {
            hr = dialog->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_PICKFOLDERS);
            if(SUCCEEDED(hr))
            {
                hr = dialog->Show(nullptr);
                if(SUCCEEDED(hr))
                {
                    IShellItem* selected_item;
                    hr = dialog->GetResult(&selected_item);
                    if(SUCCEEDED(hr))
                    {
                        TCHAR* selected_path = nullptr;
                        hr = selected_item->GetDisplayName(SIGDN_FILESYSPATH, &selected_path);
                        if(SUCCEEDED(hr))
                        {
                            result = std::wstring(selected_path);
                            CoTaskMemFree(selected_path);
                            success = true;
                        }

                        selected_item->Release();
                    }
                }
                else if(hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
                {
                    success = true;
                }
            }
        }

        dialog->Release();
    }

    if(success)
    {
        if(!result.empty())
        {
            SetDlgItemText(IDC_SAVE_CUSTOM_PATH, result.c_str());
        }
    }
    else
    {
        LOG_INFO("Failure to get a path from the directory-select dialog");
    }
}

void PreferencesSaving::UpdateFormatPreview(int edit_id, int preview_id, bool is_path)
{
    CWindow preview_item = GetDlgItem(preview_id);
    assert(preview_item != nullptr);

    LRESULT format_text_length_result = SendDlgItemMessage(edit_id, WM_GETTEXTLENGTH, 0, 0);
    if(format_text_length_result > 0)
    {
        size_t format_text_length = (size_t)format_text_length_result;
        TCHAR* format_text_buffer = new TCHAR[format_text_length+1]; // +1 for null-terminator
        GetDlgItemText(edit_id, format_text_buffer, format_text_length+1);
        std::string format_text = from_tstring(std::tstring_view{format_text_buffer, format_text_length});
        delete[] format_text_buffer;

        titleformat_object::ptr format_script;
        bool compile_success = titleformat_compiler::get()->compile(format_script, format_text.c_str());
        if(compile_success)
        {
            metadb_handle_ptr preview_track = nullptr;

            service_ptr_t<playback_control> playback = playback_control::get();
            if(playback->get_now_playing(preview_track))
            {
                LOG_INFO("Playback is currently active, using the now-playing track for format preview");
            }
            else
            {
                pfc::list_t<metadb_handle_ptr> selection;

                service_ptr_t<playlist_manager> playlist = playlist_manager::get();
	            playlist->activeplaylist_get_selected_items(selection);

                if(selection.get_count() > 0)
                {
                    LOG_INFO("Using the first selected item for format preview");
                    preview_track = selection[0];
                }
                else if(playlist->activeplaylist_get_item_handle(preview_track, 0))
                {
                    LOG_INFO("No selection available, using the first playlist item for format preview");
                }
                else
                {
                    LOG_INFO("No selection available & no active playlist. There will be no format preview");
                }
            }

            if(preview_track != nullptr)
            {
                titleformat_filename_filter filter_impl;
                titleformat_text_filter* filter = nullptr;
                if(is_path)
                {
                    filter = &filter_impl;
                }

                pfc::string8 formatted;
                bool format_success = preview_track->format_title(nullptr, formatted, format_script, filter);
                if(format_success)
                {
                    if(!is_path)
                    {
                        formatted.fix_filename_chars();
                    }

                    std::tstring preview = to_tstring(formatted);
                    preview_item.SetWindowText(preview.c_str());
                }
                else
                {
                    preview_item.SetWindowText(_T("<Unexpected formatting error>"));
                }
            }
            else
            {
                preview_item.SetWindowText(_T(""));
            }
        }
        else
        {
            preview_item.SetWindowText(_T("<Invalid format>"));
        }
    }
    else
    {
        preview_item.SetWindowText(_T(""));
    }
}

void PreferencesSaving::SetCustomPathEnabled()
{
    // NOTE: the auto-combo config sets item-data to the integral representation of that option's enum value
    LRESULT ui_index = SendDlgItemMessage(IDC_SAVE_DIRECTORY_CLASS, CB_GETCURSEL, 0, 0);
    LRESULT logical_value = SendDlgItemMessage(IDC_SAVE_DIRECTORY_CLASS, CB_GETITEMDATA, ui_index, 0);
    assert(logical_value != CB_ERR);
    SaveDirectoryClass dir_class = static_cast<SaveDirectoryClass>(logical_value);
    bool has_custom_path = (dir_class == SaveDirectoryClass::Custom);

    GetDlgItem(IDC_SAVE_CUSTOM_PATH).EnableWindow(has_custom_path);
    GetDlgItem(IDC_SAVE_CUSTOM_PATH_BROWSE).EnableWindow(has_custom_path);
    if(has_custom_path)
    {
        UpdateFormatPreview(IDC_SAVE_CUSTOM_PATH, IDC_SAVE_CUSTOM_PATH_PREVIEW, true);
    }
    else
    {
        GetDlgItem(IDC_SAVE_CUSTOM_PATH_PREVIEW).SetWindowText(_T(""));
    }
}

void PreferencesSaving::SetMethodFieldsEnabled()
{
    // NOTE: the auto-combo config sets item-data to the integral representation of that option's enum value
    LRESULT ui_index = SendDlgItemMessage(IDC_SAVE_METHOD_COMBO, CB_GETCURSEL, 0, 0);
    LRESULT logical_value = SendDlgItemMessage(IDC_SAVE_METHOD_COMBO, CB_GETITEMDATA, ui_index, 0);
    assert(logical_value != CB_ERR);
    const SaveMethod method = static_cast<SaveMethod>(logical_value);

    GetDlgItem(IDC_SAVE_TAG_SYNCED).EnableWindow(method == SaveMethod::Id3Tag);
    GetDlgItem(IDC_SAVE_TAG_UNSYNCED).EnableWindow(method == SaveMethod::Id3Tag);
    GetDlgItem(IDC_SAVE_FILENAME_FORMAT).EnableWindow(method == SaveMethod::LocalFile);
    GetDlgItem(IDC_SAVE_DIRECTORY_CLASS).EnableWindow(method == SaveMethod::LocalFile);
    GetDlgItem(IDC_SAVE_CUSTOM_PATH_BROWSE).EnableWindow(method == SaveMethod::LocalFile);

    if(method == SaveMethod::LocalFile)
    {
        UpdateFormatPreview(IDC_SAVE_FILENAME_FORMAT, IDC_SAVE_FILE_NAME_PREVIEW, true);
        SetCustomPathEnabled();
    }
    else
    {
        GetDlgItem(IDC_SAVE_CUSTOM_PATH).EnableWindow(false);
        GetDlgItem(IDC_SAVE_CUSTOM_PATH_BROWSE).EnableWindow(false);
        GetDlgItem(IDC_SAVE_CUSTOM_PATH_PREVIEW).SetWindowText(_T(""));
        GetDlgItem(IDC_SAVE_FILE_NAME_PREVIEW).SetWindowText(_T(""));
    }
}

class PreferencesSavingImpl : public preferences_page_impl<PreferencesSaving>
{
public:
    const char* get_name() { return "Saving"; }
    GUID get_guid() { return GUID_PREFERENCES_PAGE_SAVING; }
    GUID get_parent_guid() { return GUID_PREFERENCES_PAGE_ROOT; }
};
static preferences_page_factory_t<PreferencesSavingImpl> g_preferences_page_saving_factory;

