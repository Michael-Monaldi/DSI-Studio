#include <QFileDialog>
#include <QDateTime>
#include <QUrl>
#include <QMessageBox>
#include <QProgressDialog>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QAction>
#include <QStyleFactory>
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "TIPL/tipl.hpp"
#include <regtoolbox.h>
#include <qmessagebox.h>
#include "filebrowser.h"
#include "reconstruction/reconstruction_window.h"
#include "prog_interface_static_link.h"
#include "tracking/tracking_window.h"
#include "mainwindow.h"
#include "dicom/dicom_parser.h"
#include "ui_mainwindow.h"
#include "view_image.h"
#include "mapping/atlas.hpp"
#include "libs/gzip_interface.hpp"
#include "connectometry/group_connectometry_analysis.h"
#include "libs/tracking/fib_data.hpp"
#include "manual_alignment.h"
#include "connectometry/createdbdialog.h"
#include "connectometry/db_window.h"
#include "connectometry/group_connectometry.hpp"
#include "program_option.hpp"
#include "libs/dsi/image_model.hpp"
#include "auto_track.h"
#include <filesystem>
#include "xnat_dialog.h"

console_stream console;



bool is_main_thread(void);
void console_stream::update_text_edit(QTextEdit* log_window)
{
    if(has_new_line)
    {
        has_new_line = false;
        QStringList strSplitted;
        {
            std::lock_guard<std::mutex> lock(edit_buf);
            strSplitted = buf.split("\n");
            buf = strSplitted.back();
        }
        log_window->moveCursor (QTextCursor::End);
        for(int i = 0; i+1 < strSplitted.size(); i++)
            log_window->append(strSplitted.at(i));
    }
}
std::basic_streambuf<char>::int_type console_stream::overflow(std::basic_streambuf<char>::int_type v)
{
    std::lock_guard<std::mutex> lock(edit_buf);
    buf.push_back(char(v));
    if (v == '\n')
        has_new_line = true;
    return v;
}

std::streamsize console_stream::xsputn(const char *p, std::streamsize n)
{
    std::lock_guard<std::mutex> lock(edit_buf);
    buf += p;
    return n;
}


extern std::string arg_file_name;
std::vector<tracking_window*> tracking_windows;
MainWindow::MainWindow(QWidget *parent) :
        QMainWindow(parent),
        ui(new Ui::MainWindow)
{
    setAcceptDrops(true);
    ui->setupUi(this);
    ui->tabWidget->setCurrentIndex(0);
    ui->styles->addItems(QStringList("default") << QStyleFactory::keys());
    ui->styles->setCurrentText(settings.value("styles","Fusion").toString());

    ui->recentFib->setColumnCount(3);
    ui->recentFib->setColumnWidth(0,300);
    ui->recentFib->setColumnWidth(1,250);
    ui->recentFib->setColumnWidth(2,150);
    ui->recentFib->setAlternatingRowColors(true);
    ui->recentSrc->setColumnCount(3);
    ui->recentSrc->setColumnWidth(0,300);
    ui->recentSrc->setColumnWidth(1,250);
    ui->recentSrc->setColumnWidth(2,150);
    ui->recentSrc->setAlternatingRowColors(true);
    QObject::connect(ui->recentFib,SIGNAL(cellDoubleClicked(int,int)),this,SLOT(open_fib_at(int,int)));
    QObject::connect(ui->recentSrc,SIGNAL(cellDoubleClicked(int,int)),this,SLOT(open_src_at(int,int)));
    updateRecentList();

    if (settings.contains("WORK_PATH"))
        ui->workDir->addItems(settings.value("WORK_PATH").toStringList());
    else
        ui->workDir->addItem(QDir::currentPath());

    ui->toolBox->setCurrentIndex(0);

    if(!arg_file_name.empty())
        openFile(arg_file_name.c_str());
    qApp->installEventFilter(this);
}
bool MainWindow::eventFilter(QObject*, QEvent*)
{
    console.update_text_edit(ui->console);
    return false;
}
void MainWindow::openFile(QString file_name)
{
    if(QFileInfo(file_name).isDir())
    {
        QStringList fib_list = QDir(file_name).entryList(QStringList("*fib.gz"),QDir::Files|QDir::NoSymLinks);
        QStringList tt_list = QDir(file_name).entryList(
                    QStringList("*tt.gz") <<
                    QString("*trk.gz") <<
                    QString("*trk") <<
                    QString("*tck"),QDir::Files|QDir::NoSymLinks);
        if(!fib_list.empty())
            loadFib(file_name + "/" + fib_list[0]);
        else
        {
            loadFib(file_name + "/" + tt_list[0]);
            tt_list.removeAt(0);
        }
        if(!tt_list.empty())
        {
            for(int i = 0;i < tt_list.size();++i)
                tt_list[i] = file_name + "/" + tt_list[i];
            tracking_windows.back()->tractWidget->load_tracts(tt_list);
        }
        return;
    }
    if(!QFileInfo(file_name).exists())
    {
        if(file_name[0] == '-') // Mac pass a variable
            return;
        QMessageBox::information(this,"Error",QString("Cannot find ") +
        file_name + " at current dir: " + QDir::current().dirName());
    }
    else
    {
        if(QString(file_name).endsWith("fib.gz") ||
           QString(file_name).endsWith("trk.gz") ||
           QString(file_name).endsWith("tt.gz") ||
           QString(file_name).endsWith("trk") ||
           QString(file_name).endsWith("tck"))
        {
            loadFib(file_name);
        }
        else
        if(QString(file_name).endsWith("src.gz"))
        {
            loadSrc(QStringList() << file_name);
        }
        else
        if(QString(file_name).endsWith(".nii") ||
           QString(file_name).endsWith(".nii.gz") ||
                QString(file_name).endsWith(".dcm"))
        {
            view_image* dialog = new view_image(this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            if(!dialog->open(QStringList() << file_name))
            {
                delete dialog;
                return;
            }
            dialog->show();
        }
        else {
            QMessageBox::information(this,"error","Unsupported file extension");
        }
    }
}
void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if(event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    event->acceptProposedAction();
    QList<QUrl> droppedUrls = event->mimeData()->urls();
    int droppedUrlCnt = droppedUrls.size();
    QStringList files;
    for(int i = 0; i < droppedUrlCnt; i++)
        files << droppedUrls[i].toLocalFile();

    if(files.size() == 1)
        openFile(files[0]);
}

void MainWindow::open_fib_at(int row,int)
{
    loadFib(ui->recentFib->item(row,1)->text() + "/" +
            ui->recentFib->item(row,0)->text());
}

void MainWindow::open_src_at(int row,int)
{
    loadSrc(QStringList() << (ui->recentSrc->item(row,1)->text() + "/" +
            ui->recentSrc->item(row,0)->text()));
}


void MainWindow::closeEvent(QCloseEvent *event)
{
    for(size_t index = 0;index < tracking_windows.size();++index)
    if(tracking_windows[index])
        {
            tracking_windows[index]->closeEvent(event);
            if(!event->isAccepted())
                return;
            delete tracking_windows[index];
        }
    QMainWindow::closeEvent(event);
}
MainWindow::~MainWindow()
{
    QStringList workdir_list;
    for (int index = 0;index < 10 && index < ui->workDir->count();++index)
        workdir_list << ui->workDir->itemText(index);
    std::swap(workdir_list[0],workdir_list[ui->workDir->currentIndex()]);
    settings.setValue("WORK_PATH", workdir_list);
    delete ui;

}


void MainWindow::updateRecentList(void)
{
    {
        QStringList file_list = settings.value("recentFibFileList").toStringList();
        ui->recentFib->clear();
        ui->recentFib->setRowCount(file_list.size());
        for (int index = 0;index < file_list.size();++index)
        {
            ui->recentFib->setRowHeight(index,20);
            ui->recentFib->setItem(index, 0, new QTableWidgetItem(QFileInfo(file_list[index]).fileName()));
            ui->recentFib->setItem(index, 1, new QTableWidgetItem(QFileInfo(file_list[index]).absolutePath()));
            ui->recentFib->setItem(index, 2, new QTableWidgetItem(QFileInfo(file_list[index]).lastModified().toString()));
            ui->recentFib->item(index,0)->setFlags(ui->recentFib->item(index,0)->flags() & ~Qt::ItemIsEditable);
            ui->recentFib->item(index,1)->setFlags(ui->recentFib->item(index,1)->flags() & ~Qt::ItemIsEditable);
            ui->recentFib->item(index,2)->setFlags(ui->recentFib->item(index,2)->flags() & ~Qt::ItemIsEditable);
        }
    }
    {
        QStringList file_list = settings.value("recentSrcFileList").toStringList();
        ui->recentSrc->clear();
        ui->recentSrc->setRowCount(file_list.size());
        for (int index = 0;index < file_list.size();++index)
        {
            ui->recentSrc->setRowHeight(index,20);
            ui->recentSrc->setItem(index, 0, new QTableWidgetItem(QFileInfo(file_list[index]).fileName()));
            ui->recentSrc->setItem(index, 1, new QTableWidgetItem(QFileInfo(file_list[index]).absolutePath()));
            ui->recentSrc->setItem(index, 2, new QTableWidgetItem(QFileInfo(file_list[index]).lastModified().toString()));
            ui->recentSrc->item(index,0)->setFlags(ui->recentSrc->item(index,0)->flags() & ~Qt::ItemIsEditable);
            ui->recentSrc->item(index,1)->setFlags(ui->recentSrc->item(index,1)->flags() & ~Qt::ItemIsEditable);
            ui->recentSrc->item(index,2)->setFlags(ui->recentSrc->item(index,2)->flags() & ~Qt::ItemIsEditable);
        }
    }
    QStringList header;
    header << "File Name" << "Directory" << "Date";
    ui->recentFib->setHorizontalHeaderLabels(header);
    ui->recentSrc->setHorizontalHeaderLabels(header);
}

void MainWindow::addFib(QString filename)
{
    // update recent file list
    QStringList files = settings.value("recentFibFileList").toStringList();
    files.removeAll(filename);
    files.prepend(filename);
    while (files.size() > MaxRecentFiles)
        files.removeLast();
    settings.setValue("recentFibFileList", files);
    updateRecentList();
}

void MainWindow::addSrc(QString filename)
{
    // update recent file list
    QStringList files = settings.value("recentSrcFileList").toStringList();
    files.removeAll(filename);
    files.prepend(filename);
    while (files.size() > MaxRecentFiles)
        files.removeLast();
    settings.setValue("recentSrcFileList", files);
    updateRecentList();
}
void shift_track_for_tck(std::vector<std::vector<float> >& loaded_tract_data,tipl::shape<3>& geo);
void MainWindow::loadFib(QString filename,bool presentation_mode)
{
    std::string file_name = filename.toStdString();
    progress prog_("loading ",QFileInfo(filename).baseName().toStdString().c_str(),true);
    std::shared_ptr<fib_data> new_handle(new fib_data);
    if (!new_handle->load_from_file(&*file_name.begin()))
    {
        if(!progress::aborted())
            QMessageBox::information(this,"error",new_handle->error_msg.c_str());
        return;
    }
    if(new_handle->has_high_reso)
    {
        int result = QMessageBox::information(this,"DSI Studio","The FIB has large image data. Use surrogate interface to speed up?",
                                 QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel);
        if(result == QMessageBox::Cancel)
            return;
        if(result == QMessageBox::No)
            new_handle = new_handle->high_reso;
    }

    QDir::setCurrent(QFileInfo(filename).absolutePath());

    tracking_windows.push_back(new tracking_window(this,new_handle));
    tracking_windows.back()->setAttribute(Qt::WA_DeleteOnClose);
    tracking_windows.back()->setWindowTitle(filename);
    if(presentation_mode)
    {
        tracking_windows.back()->command("load_workspace",QFileInfo(filename).absolutePath());
        tracking_windows.back()->command("presentation_mode");
    }
    else
    {
        addFib(filename);
        add_work_dir(QFileInfo(filename).absolutePath());
    }
    tracking_windows.back()->showNormal();
    tracking_windows.back()->resize(1200,700);
    if(filename.endsWith("trk.gz") || filename.endsWith("trk") || filename.endsWith("tck") || filename.endsWith("tt.gz"))
    {
        tracking_windows.back()->tractWidget->load_tracts(QStringList() << filename);
        if(filename.endsWith("tck"))
        {
            tipl::shape<3> geo;
            shift_track_for_tck(tracking_windows.back()->tractWidget->tract_models.back()->get_tracts(),geo);
        }
    }

}

void MainWindow::loadSrc(QStringList filenames)
{
    if(filenames.empty())
    {
        QMessageBox::information(this,"Error","Cannot find SRC.gz files in the directory. Please create SRC files first.");
        return;
    }
    try
    {
        reconstruction_window* new_mdi = new reconstruction_window(filenames,this);
        new_mdi->setAttribute(Qt::WA_DeleteOnClose);
        new_mdi->show();
        QDir::setCurrent(QFileInfo(filenames[0]).absolutePath());
        if(filenames.size() == 1)
        {
            addSrc(filenames[0]);
            add_work_dir(QFileInfo(filenames[0]).absolutePath());
        }
    }
    catch(...)
    {

    }

}


void MainWindow::openRecentFibFile(void)
{
    QAction *action = qobject_cast<QAction *>(sender());
    loadFib(action->data().toString());
}
void MainWindow::openRecentSrcFile(void)
{
    QAction *action = qobject_cast<QAction *>(sender());
    loadSrc(QStringList() << action->data().toString());
}

void MainWindow::on_OpenDICOM_clicked()
{
    QStringList filenames = QFileDialog::getOpenFileNames(
                                this,
                                "Open Images files",
                                ui->workDir->currentText(),
                                "Image files (*.dcm *.hdr *.nii *nii.gz *.fdf *.nhdr 2dseq subject);;All files (*)" );
    if ( filenames.isEmpty() )
        return;

    add_work_dir(QFileInfo(filenames[0]).absolutePath());
    if(QFileInfo(filenames[0]).completeBaseName() == "subject")
    {
        tipl::io::bruker_info subject_file;
        if(!subject_file.load_from_file(filenames[0].toLocal8Bit().begin()))
            return;
        QString dir = QFileInfo(filenames[0]).absolutePath();
        filenames.clear();
        for(unsigned int i = 1;i < 100;++i)
        if(QDir(dir + "/" +QString::number(i)).exists())
        {
            bool is_dwi =false;
            // has dif info in the method file
            {
                tipl::io::bruker_info method_file;
                QString method_name = dir + "/" +QString::number(i)+"/method";
                if(method_file.load_from_file(method_name.toLocal8Bit().begin()) &&
                   method_file["PVM_DwEffBval"].length())
                    is_dwi = true;
            }
            // has dif info in the imnd file
            {
                tipl::io::bruker_info imnd_file;
                QString imnd_name = dir + "/" +QString::number(i)+"/imnd";
                if(imnd_file.load_from_file(imnd_name.toLocal8Bit().begin()) &&
                   imnd_file["IMND_diff_b_value"].length())
                    is_dwi = true;
            }
            if(is_dwi)
                filenames.push_back(dir + "/" +QString::number(i)+"/pdata/1/2dseq");
        }
        if(filenames.size() == 0)
        {
            QMessageBox::information(this,"Error","No diffusion data in this subject");
            return;
        }
        std::string file_name(subject_file["SUBJECT_study_name"]);
        file_name.erase(std::remove(file_name.begin(),file_name.end(),' '),file_name.end());
        dicom_parser* dp = new dicom_parser(filenames,this);
        dp->set_name(dir + "/" + file_name.c_str() + ".src.gz");
        dp->setAttribute(Qt::WA_DeleteOnClose);
        dp->showNormal();
        return;
    }

    if(QFileInfo(filenames[0]).completeSuffix() == "dcm")
    {
        QString sel = QString("*.")+QFileInfo(filenames[0]).suffix();
        QDir directory = QFileInfo(filenames[0]).absoluteDir();
        QStringList file_list = directory.entryList(QStringList(sel),QDir::Files|QDir::NoSymLinks);
        if(file_list.size() > filenames.size())
        {
            QString msg =
              QString("There are %1 %2 files in the directory. Select all?").arg(file_list.size()).arg(QFileInfo(filenames[0]).suffix());
            int result = QMessageBox::information(this,"Input images",msg,
                                     QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel);
            if(result == QMessageBox::Cancel)
                return;
            if(result == QMessageBox::Yes)
            {
                filenames = file_list;
                for(int index = 0;index < filenames.size();++index)
                    filenames[index] = directory.absolutePath() + "/" + filenames[index];
            }
        }
    }
    dicom_parser* dp = new dicom_parser(filenames,this);
    dp->setAttribute(Qt::WA_DeleteOnClose);
    dp->showNormal();
    if(dp->dwi_files.empty())
        dp->close();
}

void MainWindow::on_Reconstruction_clicked()
{
    QStringList filenames = QFileDialog::getOpenFileNames(
                           this,
                           "Open Src files",
                           ui->workDir->currentText(),
                           "Src files (*src.gz *.src);;Histology images (*.jpg *.tif);;All files (*)" );
    if (filenames.isEmpty())
        return;
    loadSrc(filenames);
}

void MainWindow::on_FiberTracking_clicked()
{
    QString filename = QFileDialog::getOpenFileName(
                           this,
                           "Open Fib files",
                           ui->workDir->currentText(),
                           "Fib files (*fib.gz *.fib);;Image files (*nii.gz *.nii 2dseq);;All files (*)");
    if (filename.isEmpty())
        return;
    loadFib(filename);
}

void check_name(std::string& name)
{
    for(unsigned int index = 0;index < name.size();++index)
        if((name[index] < '0' || name[index] > '9') &&
           (name[index] < 'a' || name[index] > 'z') &&
           (name[index] < 'A' || name[index] > 'Z') &&
                name[index] != '.')
            name[index] = '_';
}

bool RenameDICOMToDir(QString FileName, QString ToDir,QString& NewName)
{
    std::string person, sequence, imagename;
    {
        tipl::io::dicom header;
        if (!header.load_from_file(FileName.toLocal8Bit().begin()))
            return false;

        header.get_patient(person);
        header.get_sequence(sequence);
        header.get_image_name(imagename);
    }
    check_name(person);
    check_name(sequence);
    check_name(imagename);

    QString Person(person.c_str()), Sequence(sequence.c_str()),
    ImageName(imagename.c_str());
    ToDir += "/";
    ToDir += Person;
    if (!QDir(ToDir).exists())
    {
        if(!QDir(ToDir).mkdir(ToDir))
        {
            std::cout << "cannot create dir " << ToDir.toStdString() << std::endl;
            return false;
        }
    }
    ToDir += "/";
    ToDir += Sequence;
    if (!QDir(ToDir).exists())
    {
        if(!QDir(ToDir).mkdir(ToDir))
        {
            std::cout << "cannot create dir " << ToDir.toStdString() << std::endl;
            return false;
        }
    }
    ToDir += "/";
    ToDir += ImageName;
    NewName = ToDir;
    return true;
}
bool RenameDICOMToDir(QString FileName, QString ToDir)
{
    QString NewName;
    if(!RenameDICOMToDir(FileName,ToDir,NewName))
        return false;
    std::cout << FileName.toStdString() << "->" << NewName.toStdString() << std::endl;
    return QFile::rename(FileName,NewName);
}

void MainWindow::on_RenameDICOM_clicked()
{
    QStringList filenames = QFileDialog::getOpenFileNames(
                                this,
                                "Open DICOM files",
                                ui->workDir->currentText(),
                                "All files (*)" );
    if ( filenames.isEmpty() )
        return;
    progress prog_("Rename DICOM Files");
    for (unsigned int index = 0;progress::at(index,filenames.size());++index)
        RenameDICOMToDir(filenames[index],QFileInfo(filenames[index]).absolutePath());
}


void MainWindow::add_work_dir(QString dir)
{
    if(ui->workDir->findText(dir) != -1)
        ui->workDir->removeItem(ui->workDir->findText(dir));
    ui->workDir->insertItem(0,dir);
    ui->workDir->setCurrentIndex(0);
}


void MainWindow::on_browseDir_clicked()
{
    QString filename =
        QFileDialog::getExistingDirectory(this,"Browse Directory",
                                          ui->workDir->currentText());
    if ( filename.isEmpty() )
        return;
    add_work_dir(filename);
}


QStringList GetSubDir(QString Dir,bool recursive = true)
{
    QStringList sub_dirs;
    QStringList dirs = QDir(Dir).entryList(QStringList("*"),
                                            QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot);
    if(recursive)
        sub_dirs << Dir;
    for(int index = 0;index < dirs.size();++index)
    {
        QString new_dir = Dir + "/" + dirs[index];
        if(recursive)
            sub_dirs << GetSubDir(new_dir,recursive);
        else
            sub_dirs << new_dir;
    }
    return sub_dirs;
}
void MainWindow::on_RenameDICOMDir_clicked()
{
    QString path =
        QFileDialog::getExistingDirectory(this,"Browse Directory",
                                          ui->workDir->currentText());
    if ( path.isEmpty() )
        return;
    QStringList dirs = GetSubDir(path);
    progress prog_("Renaming DICOM");
    for(int index = 0;progress::at(index,dirs.size());++index)
    {
        QStringList files = QDir(dirs[index]).entryList(QStringList("*"),
                                    QDir::Files | QDir::NoSymLinks);
        for(int j = 0;j < files.size() && index < dirs.size();++j)
        {
            progress::show(files[j].toLocal8Bit().begin());
            RenameDICOMToDir(dirs[index] + "/" + files[j],path);
        }
    }
}

void MainWindow::on_vbc_clicked()
{
    CreateDBDialog* new_mdi = new CreateDBDialog(this,true);
    new_mdi->setAttribute(Qt::WA_DeleteOnClose);
    new_mdi->show();
}

void MainWindow::on_averagefib_clicked()
{
    CreateDBDialog* new_mdi = new CreateDBDialog(this,false);
    new_mdi->setAttribute(Qt::WA_DeleteOnClose);
    new_mdi->show();
}

bool parse_dwi(QStringList file_list,std::vector<std::shared_ptr<DwiHeader> >& dwi_files);
bool load_4d_nii(const char* file_name,std::vector<std::shared_ptr<DwiHeader> >& dwi_files,bool need_bvalbvec);
QString get_dicom_output_name(QString file_name,QString file_extension,bool add_path);




QStringList search_files(QString dir,QString filter);
void MainWindow::on_batch_reconstruction_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(
                                this,
                                "Open directory",
                                ui->workDir->currentText());
    if(dir.isEmpty())
        return;

    loadSrc(search_files(dir,"*src.gz"));
}

void MainWindow::on_view_image_clicked()
{
    QStringList filename = QFileDialog::getOpenFileNames(
                                this,
                                "Open Image",
                                ui->workDir->currentText(),
                                "image files (*.nii *nii.gz *.dcm 2dseq *fib.gz *src.gz)" );
    if(filename.isEmpty())
        return;
    view_image* dialog = new view_image(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    if(!dialog->open(filename))
    {
        delete dialog;
        return;
    }
    dialog->show();
}

void MainWindow::on_workDir_currentTextChanged(const QString &arg1)
{
    QDir::setCurrent(arg1);
}


bool MainWindow::load_db(std::shared_ptr<group_connectometry_analysis>& database,QString& filename)
{
    filename = QFileDialog::getOpenFileName(
                           this,
                           "Open Database files",
                           ui->workDir->currentText(),
                           "Database files (*db?fib.gz);;All files (*)");
    if (filename.isEmpty())
        return false;
    QDir::setCurrent(QFileInfo(filename).absolutePath());
    add_work_dir(QFileInfo(filename).absolutePath());
    database = std::make_shared<group_connectometry_analysis>();
    progress prog_("reading connectometry db");
    if(!database->load_database(filename.toLocal8Bit().begin()))
    {
        QMessageBox::information(this,"Error",database->error_msg.c_str());
        return false;
    }
    return true;
}

void MainWindow::on_open_db_clicked()
{
    QString filename;
    std::shared_ptr<group_connectometry_analysis> database;
    if(!load_db(database,filename))
        return;
    db_window* db = new db_window(this,database);
    db->setWindowTitle(filename);
    db->setAttribute(Qt::WA_DeleteOnClose);
    db->show();
}

void MainWindow::on_group_connectometry_clicked()
{
    QString filename;
    std::shared_ptr<group_connectometry_analysis> database;
    if(!load_db(database,filename))
        return;
    group_connectometry* group_cnt = new group_connectometry(this,database,filename);
    group_cnt->setAttribute(Qt::WA_DeleteOnClose);
    group_cnt->show();
}

int rec(program_option& po);
int trk(program_option& po);
int src(program_option& po);
int ana(program_option& po);
int exp(program_option& po);
int atl(program_option& po);
int cnt(program_option& po);
int vis(program_option& po);
int ren(program_option& po);
int atk(program_option& po);
int reg(program_option& po);
int xnat(program_option& po);
void MainWindow::on_run_cmd_clicked()
{
    program_option po;
    if(!po.parse(ui->cmd_line->text().toStdString()))
    {
        QMessageBox::information(this,"Error",po.error_msg.c_str());
        return;
    }
    if (!po.has("action"))
    {
        std::cout << "invalid command, use --help for more detail" << std::endl;
        return;
    }
    QDir::setCurrent(QFileInfo(po.get("source").c_str()).absolutePath());
    if(po.get("action") == std::string("rec"))
        rec(po);
    if(po.get("action") == std::string("trk"))
        trk(po);
    if(po.get("action") == std::string("src"))
        src(po);
    if(po.get("action") == std::string("ana"))
        ana(po);
    if(po.get("action") == std::string("exp"))
        exp(po);
    if(po.get("action") == std::string("atl"))
        atl(po);
    if(po.get("action") == std::string("cnt"))
        cnt(po);
    if(po.get("action") == std::string("vis"))
        vis(po);
    if(po.get("action") == std::string("ren"))
        ren(po);
    if(po.get("action") == std::string("atk"))
        atk(po);
    if(po.get("action") == std::string("reg"))
        reg(po);
    if(po.get("action") == std::string("xnat"))
        xnat(po);
}


void calculate_shell(const std::vector<float>& bvalues,std::vector<unsigned int>& shell);
bool is_dsi_half_sphere(const std::vector<unsigned int>& shell);
bool need_scheme_balance(const std::vector<unsigned int>& shell);

extern std::vector<std::string> fa_template_list,iso_template_list;
void MainWindow::on_ReconstructSRC_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(
                                this,
                                "Open directory",
                                ui->workDir->currentText());
    if(dir.isEmpty())
        return;

    QStringList list = search_files(dir,"*src.gz");

    for(int i = 0;i < list.size();++i)
    {
        std::shared_ptr<ImageModel> handle(std::make_shared<ImageModel>());
        if (!handle->load_from_file(list[i].toLocal8Bit().begin()))
        {
            if(!handle->error_msg.empty())
                QMessageBox::critical(this,"ERROR",handle->error_msg.c_str());
            return;
        }

        handle->voxel.method_id = 7; // QSDR
        handle->voxel.ti.init(8); // odf order of 8
        handle->voxel.output_odf = true; // output ODF
        handle->voxel.other_output = "all";
        handle->voxel.thread_count = std::thread::hardware_concurrency();

        //checking half shell
        {
            handle->voxel.half_sphere = handle->is_dsi_half_sphere();
            handle->voxel.scheme_balance = handle->need_scheme_balance();
        }

        if (!handle->reconstruction())
        {
            QMessageBox::information(this,"ERROR",handle->error_msg.c_str());
            return;
        }
    }
}

void MainWindow::on_set_dir_clicked()
{
    QString dir =
        QFileDialog::getExistingDirectory(this,"Browse Directory","");
    if ( dir.isEmpty() )
        return;
    QDir::setCurrent(dir);
    ui->pwd->setText(QString("[%1]$ ./dsi_studio ").arg(QDir().current().absolutePath()));
}

bool load_image_from_files(QStringList filenames,tipl::image<3>& ref,tipl::vector<3>& vs,tipl::matrix<4,4>& trans);

void MainWindow::on_linear_reg_clicked()
{
    QStringList filename1 = QFileDialog::getOpenFileNames(
            this,"Open Subject Image",ui->workDir->currentText(),
            "Images (*.nii *nii.gz *.dcm);;All files (*)" );
    if(filename1.isEmpty())
        return;


    QStringList filename2 = QFileDialog::getOpenFileNames(
            this,"Open Template Image",QFileInfo(filename1[0]).absolutePath(),
            "Images (*.nii *nii.gz *.dcm);;All files (*)" );
    if(filename2.isEmpty())
        return;


    tipl::image<3> ref1,ref2;
    tipl::vector<3> vs1,vs2;
    tipl::matrix<4,4> t1,t2;
    if(!load_image_from_files(filename1,ref1,vs1,t1) ||
       !load_image_from_files(filename2,ref2,vs2,t2))
        return;
    std::shared_ptr<manual_alignment> manual(new manual_alignment(this,ref1,vs1,ref2,vs2,tipl::reg::affine,tipl::reg::mutual_info));
    manual->nifti_srow = t2;
    if(manual->exec() != QDialog::Accepted)
        return;
}

void MainWindow::on_nonlinear_reg_clicked()
{
    RegToolBox* rt = new RegToolBox(this);
    rt->setAttribute(Qt::WA_DeleteOnClose);
    rt->showNormal();
}

std::string quality_check_src_files(QString dir);
void show_info_dialog(const std::string& title,const std::string& result);
void MainWindow::on_SRC_qc_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(
                                this,
                                "Open directory",
                                ui->workDir->currentText());
    if(dir.isEmpty())
        return;
    progress prog_("checking SRC files");
    show_info_dialog("SRC report",quality_check_src_files(dir));
}

void MainWindow::on_parse_network_measures_clicked()
{
    QStringList filename = QFileDialog::getOpenFileNames(
            this,"Open Network Measures",ui->workDir->currentText(),
            "Text files (*.txt);;All files (*)" );
    if(filename.isEmpty())
        return;
    std::ofstream out((filename[0]+".collected.txt").toStdString().c_str());
    out << "Field\t";
    for(int i = 0;i < filename.size();++i)
        out << QFileInfo(filename[i]).baseName().toStdString() << "\t";
    out << std::endl;

    std::vector<std::string> line_output;
    for(int i = 0;i < filename.size();++i)
    {
        std::ifstream in(filename[i].toStdString().c_str());
        std::vector<std::string> node_list;
        // global measures
        size_t line_index = 0;
        while(in)
        {
            std::string t1,t2;
            in >> t1;
            if(t1 == "network_measures")
            {
                std::string nodes;
                std::getline(in,nodes);
                std::istringstream nodestream(nodes);
                std::copy(std::istream_iterator<std::string>(nodestream),
                          std::istream_iterator<std::string>(),std::back_inserter(node_list));
                break;
            }
            in >> t2;
            if(i == 0)
            {
                line_output.push_back(t1);
                line_output.back() += "\t";
            }
            line_output[line_index] += t2;
            line_output[line_index] += "\t";
            ++line_index;
        }
        // nodal measures
        std::string line;
        while(std::getline(in,line))
        {
            std::istringstream in2(line);
            std::string t1;
            in2 >> t1;
            if(t1[0] == '#' || t1[0] == ' ')
                continue;
            for(size_t k = 0;k < node_list.size();++k,++line_index)
            {
                std::string t2;
                in2 >> t2;
                if(i==0)
                {
                    line_output.push_back(t1);
                    line_output.back() += "_";
                    line_output.back() += node_list[k];
                    line_output.back() += "\t";
                }
                line_output[line_index] += t2;
                line_output[line_index] += "\t";
            }
        }
    }
    for(size_t i = 0;i < line_output.size();++i)
        out << line_output[i] << std::endl;

    QMessageBox::information(this,"DSI Studio",QString("File saved to")+filename[0]+".collected.txt");

}

void MainWindow::on_bruker_browser_clicked()
{
    FileBrowser* bw = new FileBrowser(this);
    bw->setAttribute(Qt::WA_DeleteOnClose);
    bw->showNormal();
}

void MainWindow::on_auto_track_clicked()
{
    auto_track* at = new auto_track(this);
    at->setAttribute(Qt::WA_DeleteOnClose);
    at->showNormal();
}


extern std::string src_error_msg;
void nii2src(std::string nii_name,std::string src_name,std::ostream& out)
{
    std::vector<std::shared_ptr<DwiHeader> > dwi_files;
    if(!load_4d_nii(nii_name.c_str(),dwi_files,true))
    {
        out << "ERROR: " << src_error_msg << std::endl;
        return;
    }
    out << std::filesystem::path(src_name).filename().string() << std::endl;
    if(!DwiHeader::output_src(src_name.c_str(),dwi_files,0,false))
        out << "ERROR: " << src_error_msg << std::endl;
}
void MainWindow::on_nii2src_bids_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(
                                    this,
                                    "Open BIDS Folder",
                                    ui->workDir->currentText());
    if(dir.isEmpty())
        return;
    QString output_dir = QFileDialog::getExistingDirectory(
                                    this,
                                    "Please Specify the Output Folder",
                                    QDir(dir).path()+"/derivatives");
    if(output_dir.isEmpty())
        return;
    add_work_dir(dir);
    QStringList sub_dir = QDir(dir).entryList(QStringList("sub-*"),
                                                QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot);

    if(!QDir(output_dir).exists() && !QDir().mkdir(output_dir))
    {
        QMessageBox::critical(this,"ERROR","Cannot create the output folder. Please check write privileges");
        return;
    }
    progress prog_("batch creating src");
    std::ofstream out((dir+"/log.txt").toStdString().c_str());
    out << "directory:" << dir.toStdString() << std::endl;
    int subject_num = sub_dir.size();
    for(int j = 0;progress::at(j,sub_dir.size()) && !progress::aborted();++j)
    {
        QString cur_dir = dir + "/" + sub_dir[j];
        out << "Process " << sub_dir[j].toStdString() << std::endl;
        QString dwi_folder = cur_dir + "/dwi";
        if(!QDir(dwi_folder).exists())
            dwi_folder = cur_dir;
        QStringList nifti_file_list = QDir(dwi_folder).
                entryList(QStringList("*.nii.gz") << "*.nii",QDir::Files|QDir::NoSymLinks);
        if(nifti_file_list.size() > 1)
            out << "[WARNING] Multiple NIFTI files found" << std::endl;
        for (int index = 0;index < nifti_file_list.size();++index)
        {
            out << "\t" << nifti_file_list[index].toStdString() << "->";
            std::string nii_name = dwi_folder.toStdString() + "/" + nifti_file_list[index].toStdString();
            std::string src_name = output_dir.toStdString() + "/" +
                    QFileInfo(nifti_file_list[index]).baseName().toStdString() + ".src.gz";
            nii2src(nii_name,src_name,out);
        }
        // look for sessions
        if(j < subject_num)
        {
            QStringList ses_dir = QDir(cur_dir).entryList(QStringList("ses-*"),
                                                        QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot);
            for(auto s: ses_dir)
                sub_dir.push_back(sub_dir[j] + "/" + s);
        }
    }
}

void MainWindow::on_nii2src_sf_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(
                                    this,
                                    "Open directory",
                                    ui->workDir->currentText());
    if(dir.isEmpty())
        return;
    add_work_dir(dir);
    QStringList nifti_file_list = QDir(dir).
            entryList(QStringList("*.nii.gz") << "*.nii",QDir::Files|QDir::NoSymLinks);

    progress prog_("batch creating src");
    std::ofstream out((dir+"/log.txt").toStdString().c_str());
    out << "directory:" << dir.toStdString() << std::endl;
    for(int j = 0;progress::at(j,nifti_file_list.size()) && !progress::aborted();++j)
    {
        out << nifti_file_list[j].toStdString() << "->";
        std::vector<std::shared_ptr<DwiHeader> > dwi_files;
        std::string nii_name = dir.toStdString() + "/" + nifti_file_list[j].toStdString();
        std::string src_name = dir.toStdString() + "/" +
                QFileInfo(nifti_file_list[j]).baseName().toStdString() + ".src.gz";
        nii2src(nii_name,src_name,out);
    }
}

bool dcm2src(QStringList files,std::ostream& out)
{
    if(files.empty())
        return false;
    files.sort();
    std::vector<std::shared_ptr<DwiHeader> > dicom_files;
    if(!parse_dwi(files,dicom_files) || progress::aborted())
    {
        out << "Not DICOM. Skip." << std::endl;
        return false;
    }

    // extract information
    std::string manu,make,report,sequence;
    {
        tipl::io::dicom header;
        if(!header.load_from_file(files[0].toStdString().c_str()))
        {
            out << "ERROR: cannot read image volume. Skip" << std::endl;
            return false;
        }
        header.get_sequence_id(sequence);
        header.get_text(0x0008,0x0070,manu);//Manufacturer
        header.get_text(0x0008,0x1090,make);
        std::replace(manu.begin(),manu.end(),' ',char(0));
        make.erase(std::remove(make.begin(),make.end(),' '),make.end());
        std::ostringstream info;
        info << manu.c_str() << " " << make.c_str() << " " << sequence
            << ".TE=" << header.get_float(0x0018,0x0081) << ".TR=" << header.get_float(0x0018,0x0080)  << ".";
        report = info.str();
        if(report.size() < 80)
            report.resize(80);
    }

    if(dicom_files.size() > 1) //4D NIFTI
    {
        for(unsigned int index = 0;index < dicom_files.size();++index)
        {
            if(dicom_files[index]->bvalue < 100.0f)
            {
                dicom_files[index]->bvalue = 0.0f;
                dicom_files[index]->bvec = tipl::vector<3>(0.0f,0.0f,0.0f);
            }
            if(dicom_files[index]->image.shape() != dicom_files[index]->image.shape())
            {
                out << "Inconsistent image dimension." << std::endl;
                return false;
            }
        }
        if(DwiHeader::has_b_table(dicom_files))
        {
            QString src_name = get_dicom_output_name(files[0],(std::string("_")+sequence+".src.gz").c_str(),true);
            out << "Create SRC file: " << std::filesystem::path(src_name.toStdString()).filename().string() << std::endl;
            if(!DwiHeader::output_src(src_name.toStdString().c_str(),dicom_files,0,false))
                out << "ERROR: " << src_error_msg << std::endl;
        }
        else
        {
            if(!DwiHeader::consistent_dimension(dicom_files))
                out << "[SKIPPED] Cannot save as 4D nifti due to different image dimension" << std::endl;
            else
            {
                auto dicom = dicom_files[0];
                tipl::matrix<4,4> trans;
                initial_LPS_nifti_srow(trans,dicom->image.shape(),dicom->voxel_size);

                tipl::shape<4> nifti_dim;
                std::copy(dicom->image.shape().begin(),
                          dicom->image.shape().end(),nifti_dim.begin());
                nifti_dim[3] = uint32_t(dicom_files.size());

                tipl::image<4,unsigned short> buffer(nifti_dim);
                for(unsigned int index = 0;index < dicom_files.size();++index)
                {
                    std::copy(dicom_files[index]->image.begin(),
                              dicom_files[index]->image.end(),
                              buffer.begin() + long(index*dicom_files[index]->image.size()));
                }
                QString nii_name = get_dicom_output_name(files[0],(std::string("_")+sequence+".nii.gz").c_str(),true);
                out << "Create 4D NII file: " << nii_name.toStdString() << std::endl;
                return gz_nifti::save_to_file(nii_name.toStdString().c_str(),buffer,dicom->voxel_size,trans,false,report.c_str());
            }
        }
        return true;
    }

    if(files.size() < 5)
    {
        out << "Skip." << std::endl;
        return false;
    }
    // Now handle T1W or T2FLAIR
    {
        std::sort(files.begin(),files.end(),compare_qstring());
        tipl::io::dicom_volume v;
        std::vector<std::string> file_list;
        for(int index = 0;index < files.size();++index)
            file_list.push_back(files[index].toStdString().c_str());
        if(!v.load_from_files(file_list))
        {
            out << v.error_msg.c_str() << std::endl;
            return false;
        }

        tipl::image<3> I;
        tipl::vector<3> vs;
        v >> I;
        v.get_voxel_size(vs);

        //non isotropic
        if((vs[0] < vs[2] || vs[1] < vs[2]) &&
           (tipl::min_value(vs))/(tipl::max_value(vs)) > 0.5f)
        {
            float reso = tipl::min_value(vs);
            tipl::vector<3,float> new_vs(reso,reso,reso);
            tipl::image<3> J(tipl::shape<3>(
                    int(std::ceil(float(I.width())*vs[0]/new_vs[0])),
                    int(std::ceil(float(I.height())*vs[1]/new_vs[1])),
                    int(std::ceil(float(I.depth())*vs[2]/new_vs[2]))));

            tipl::transformation_matrix<float> T1;
            T1.sr[0] = new_vs[0]/vs[0];
            T1.sr[4] = new_vs[1]/vs[1];
            T1.sr[8] = new_vs[2]/vs[2];
            tipl::resample_mt<tipl::interpolation::cubic>(I,J,T1);
            vs = new_vs;
            I.swap(J);
        }

        gz_nifti nii_out;
        tipl::flip_xy(I);
        nii_out << I;
        nii_out.set_voxel_size(vs);
        nii_out.set_descrip(report.c_str());
        std::string suffix("_");
        suffix += sequence;
        suffix += ".nii.gz";
        QString output = get_dicom_output_name(files[0],suffix.c_str(),true);
        out << "converted to NIFTI:" << std::filesystem::path(output.toStdString()).filename().string() << std::endl;
        nii_out.save_to_file(output.toStdString().c_str());
    }
    return true;
}

void dicom2src(std::string dir_,std::ostream& out)
{
    QString dir = dir_.c_str();
    QStringList sub_dir = QDir(dir).entryList(QStringList("*"),QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot);
    progress prog_("process folder");
    for(int j = 0;progress::at(j,sub_dir.size()) && !progress::aborted();++j)
    {
        progress::show(QString("process %1").arg(sub_dir[j]).toStdString());
        QStringList dir_list = GetSubDir(dir + "/" + sub_dir[j],true);
        dir_list << dir;
        for(int i = 0;i < dir_list.size();++i)
        {
            QDir cur_dir = dir_list[i];
            QStringList dicom_file_list = cur_dir.entryList(QStringList("*.dcm"),QDir::Files|QDir::NoSymLinks);
            if(dicom_file_list.empty())
                continue;
            out << QFileInfo(dir_list[i]).baseName().toStdString() << "->";
            for (int index = 0;index < dicom_file_list.size();++index)
                dicom_file_list[index] = dir_list[i] + "/" + dicom_file_list[index];
            dcm2src(dicom_file_list,out);
        }
    }
}

void MainWindow::on_dicom2nii_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(
                                this,
                                "Open directory",
                                ui->workDir->currentText());
    if(dir.isEmpty())
        return;
    progress prog("DICOM to SRC or NIFTI",true);
    add_work_dir(dir);
    std::ofstream out((dir+"/log.txt").toStdString().c_str());
    out << "directory:" << dir.toStdString() << std::endl;
    dicom2src(dir.toStdString(),out);
}

void MainWindow::on_clear_src_history_clicked()
{
    ui->recentSrc->setRowCount(0);
    settings.setValue("recentSRCFileList", QStringList());
}

void MainWindow::on_clear_fib_history_clicked()
{
    ui->recentFib->setRowCount(0);
    settings.setValue("recentFibFileList", QStringList());
}


void MainWindow::on_xnat_download_clicked()
{
    auto* xnat = new xnat_dialog(this);
    xnat->setAttribute(Qt::WA_DeleteOnClose);
    xnat->showNormal();
}


void MainWindow::on_styles_activated(int)
{
    if(ui->styles->currentText() != settings.value("styles","Fusion").toString())
    {
        settings.setValue("styles",ui->styles->currentText());
        QMessageBox::information(this,"DSI Studio","You will need to restart DSI Studio to see the change");
    }
}


void MainWindow::on_tabWidget_currentChanged(int index)
{
    if(index)
        ui->pwd->setText(QString("[%1]$ ./dsi_studio ").arg(QDir().current().absolutePath()));
}
