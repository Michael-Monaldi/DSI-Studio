#include <QString>
#include <QStringList>
#include <QFileInfo>
#include "program_option.hpp"
#include "libs/dsi/image_model.hpp"

QStringList search_files(QString dir,QString filter);


std::string quality_check_src_files(QString dir)
{
    std::ostringstream out;
    QStringList filenames = search_files(dir,"*.src.gz");
    out << "FileName\tImage dimension\tResolution\tDWI count\tShell count\tMax b-value\tB-table matched\tNeighboring DWI correlation" << std::endl;
    std::vector<unsigned int> shell;
    for(int i = 0;check_prog(i,filenames.size());++i)
    {
        out << QFileInfo(filenames[i]).baseName().toStdString() << "\t";
        ImageModel handle;
        if (!handle.load_from_file(filenames[i].toStdString().c_str()))
        {
            out << "Cannot load SRC file"  << std::endl;
            continue;
        }
        // output image dimension
        out << image::vector<3,int>(handle.voxel.dim.begin()) << "\t";
        // output image resolution
        out << handle.voxel.vs << "\t";
        // output DWI count
        out << handle.src_bvalues.size() << "\t";

        std::vector<unsigned int> cur_shell;
        handle.calculate_shell();
        cur_shell = handle.shell;
        // output shell count
        out << cur_shell.size() << "\t";
        // output max_b
        out << *std::max_element(handle.src_bvalues.begin(),handle.src_bvalues.end()) << "\t";
        // check shell structure
        if(i == 0)
        {
            shell.swap(cur_shell);
            out << "Yes\t";
        }
        else
        {
            bool match = true;
            if(shell.size() != cur_shell.size())
                match = false;
            else
            {
                for(int j = 0;j < shell.size();++j)
                    if(shell[j] != cur_shell[j])
                        match = false;
            }
            out << (match? "Yes\t" : "No\t");
        }

        // calculate neighboring DWI correlation
        out << handle.quality_control_neighboring_dwi_corr() << "\t";



        out << std::endl;
    }
    return out.str();
}

/**
 perform reconstruction
 */
int qc(void)
{
    std::string file_name = po.get("source");
    if(QFileInfo(file_name.c_str()).isDir())
    {
        file_name += "\\src_report.txt";
        std::ofstream out(file_name.c_str());
        out << quality_check_src_files(file_name.c_str());
    }
    return 0;
}
