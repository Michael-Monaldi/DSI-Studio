#include <sstream>
#include <algorithm>
#include <string>
#include <iterator>
#include <cctype>
#include <QDir>
#include <QFileInfo>
#include "fib_data.hpp"
#include "program_option.hpp"

bool find_string_case_insensitive(const std::string & str1, const std::string & str2)
{
  auto it = std::search(
    str1.begin(), str1.end(),
    str2.begin(),   str2.end(),
    [](char ch1, char ch2) { return std::toupper(ch1) == std::toupper(ch2); }
  );
  return (it != str1.end() );
}

std::string run_auto_track(
                    const std::vector<std::string>& file_list,
                    const std::vector<unsigned int>& track_id,
                    float length_ratio,
                    float tolerance,
                    float track_voxel_ratio,
                    int interpolation,int tip,
                    bool export_stat,
                    bool export_trk,
                    bool overwrite,
                    bool default_mask,
                    int& progress);

extern std::string auto_track_report;
int atk(void)
{
    std::vector<std::string> file_list;
    std::string source = po.get("source");
    if(source.find('*') != std::string::npos)
    {
        QDir dir(QFileInfo(source.c_str()).absoluteDir());
        QStringList result = dir.entryList(QStringList(QFileInfo(source.c_str()).fileName()),QDir::Files);
        std::cout << "searching " << QFileInfo(source.c_str()).fileName().toStdString() << " at " << dir.absolutePath().toStdString() << std::endl;
        for(int i = 0;i < result.size();++i)
        {
            file_list.push_back((dir.absolutePath() + "/" + result[i]).toStdString());
            std::cout << file_list.back() << std::endl;
        }
        if(file_list.empty())
        {
            std::cout << "no file found." << std::endl;
            return 1;
        }
    }
    else
        file_list.push_back(source);

    std::vector<unsigned int> track_id;
    {
        fib_data fib;
        fib.set_template_id(0);
        std::string track_id_str =
                po.get("track_id","Fasciculus,Cingulum,Aslant,Corticospinal,Corticostriatal,Corticopontine,Thalamic_R,Optic,Fornix,Corpus");
        std::replace(track_id_str.begin(),track_id_str.end(),',',' ');
        std::istringstream in(track_id_str);
        std::string str;
        while(in >> str)
        {
            try {
                track_id.push_back(uint32_t(std::stoi(str)));
            }
            catch (...)
            {
                bool find = false;
                for(size_t index = 0;index < fib.tractography_name_list.size();++index)
                    if(find_string_case_insensitive(fib.tractography_name_list[index],str))
                    {
                        track_id.push_back(index);
                        find = true;
                    }
                if(!find)
                {
                    std::cout << "invalid track_id: cannot find track name containing " << str << std::endl;
                    return false;
                }
            }

        }
        std::sort(track_id.begin(),track_id.end());
        track_id.erase(std::unique(track_id.begin(), track_id.end()), track_id.end());

        std::cout << "target tracks:";
        for(unsigned int index = 0;index < track_id.size();++index)
            std::cout << " " << fib.tractography_name_list[track_id[index]];
        std::cout << std::endl;
    }
    int progress;
    std::string error = run_auto_track(file_list,track_id,
                                po.get("length_ratio",1.25f),
                                po.get("tolerance",16.0f),
                                po.get("track_voxel_ratio",2),
                                po.get("interpolation",2),
                                po.get("tip",32),
                                po.get("export_stat",1),
                                po.get("export_trk",1),
                                po.get("overwrite",0),
                                po.get("default_mask",0),progress);
    if(error.empty())
    {
        if(po.has("report"))
        {
            std::ofstream out(po.get("report").c_str());
            out << auto_track_report;
        }
        return 0;
    }
    std::cerr << error << std::endl;
    return 1;
}
