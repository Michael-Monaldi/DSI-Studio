#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSettings>
#include <iostream>
#include <memory>
#include <mutex>
class QTextEdit;

class console_stream :  public std::basic_streambuf<char>
{
public:
    console_stream(void):std::basic_streambuf<char>(){}
    ~console_stream(void){}
    void update_text_edit(QTextEdit* log_window);
protected:
    virtual int_type overflow(int_type v) override;
    virtual std::streamsize xsputn(const char *p, std::streamsize n) override;
public:
    bool has_new_line = false;
    std::mutex edit_buf;
    QString buf;
};
extern console_stream console;



namespace Ui {
    class MainWindow;
}
class group_connectometry_analysis;
class MainWindow : public QMainWindow
{
    Q_OBJECT
    enum { MaxRecentFiles = 50 };
    void updateRecentList(void);
    QSettings settings;
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void closeEvent(QCloseEvent *event);
    Ui::MainWindow *ui;
    void addFib(QString Filename);
    void addSrc(QString Filename);
    void dragEnterEvent(QDragEnterEvent *event);
    void dropEvent(QDropEvent *event);
    void openFile(QString file_name);
    bool eventFilter(QObject *obj, QEvent *event) override;
public:
    void loadFib(QString Filename,bool presentation_mode = false);
    void loadSrc(QStringList filenames);
    void add_work_dir(QString dir);
    bool load_db(std::shared_ptr<group_connectometry_analysis>& database,QString& file_name);
private slots:
    void on_averagefib_clicked();
    void on_vbc_clicked();
    void on_RenameDICOMDir_clicked();
    void on_browseDir_clicked();
    void on_FiberTracking_clicked();
    void on_Reconstruction_clicked();
    void on_OpenDICOM_clicked();
    void on_RenameDICOM_clicked();
    void openRecentFibFile();
    void openRecentSrcFile();
    void open_fib_at(int,int);
    void open_src_at(int,int);
    void on_batch_reconstruction_clicked();
    void on_view_image_clicked();
    void on_workDir_currentTextChanged(const QString &arg1);
    void on_bruker_browser_clicked();

    void on_open_db_clicked();
    void on_group_connectometry_clicked();

    void on_run_cmd_clicked();
    void on_set_dir_clicked();
    void on_ReconstructSRC_clicked();
    void on_linear_reg_clicked();
    void on_nonlinear_reg_clicked();
    void on_SRC_qc_clicked();
    void on_parse_network_measures_clicked();
    void on_auto_track_clicked();
    void on_nii2src_bids_clicked();
    void on_nii2src_sf_clicked();
    void on_dicom2nii_clicked();
    void on_clear_src_history_clicked();
    void on_clear_fib_history_clicked();
    void on_xnat_download_clicked();
    void on_styles_activated(int index);
    void on_tabWidget_currentChanged(int index);
};

#endif // MAINWINDOW_H
