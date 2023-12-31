/******************************************************************************
 *
 * INTEL CONFIDENTIAL
 *
 * Copyright 2021 Intel Corporation.
 *
 * This software and the related documents are Intel copyrighted materials, and
 * your use of them is governed by the express license under which they were
 * provided to you ("License"). Unless the License provides otherwise, you may
 * not use, modify, copy, publish, distribute, disclose or transmit this
 * software or the related documents without Intel's prior written permission.
 *
 * This software and the related documents are provided as is, with no express
 * or implied warranties, other than those that are expressly stated in
 * the License.
 *
 ******************************************************************************/

#define MEMORY_MAP_FILENAME "default_memory_map.json"
#define DEVICE_MAP_FILENAME "default_device_map.json"
#define SILKSCREEN_MAP_FILENAME "default_silkscreen_map.json"

#include <cstring>
#include <iostream>
#include <nlohmann/json.hpp>
#include <regex>
#include <string>

#include <cpu_factory.hpp>
#include <report.hpp>
#include <summary.hpp>
#include <utils.hpp>

using json = nlohmann::json;
#define AMI_CODE
enum ReturnCode
{
  SUCCESS = 0,
  CRASHDUMP_NOT_PROVIDED = -1,
  FAILED_TO_PARSE_CRASHDUMP_TO_JSON = -2,
  INCORRECT_JSON_ROOT_NODE = -3,
  FAILED_TO_PREPARE_REPORT = -4,
};

Summary prepareSummary(const json& inputJson)
{
  Summary summary;
  summary.cpuType = getCpuType(inputJson);
  auto cpu = CpuFactory::createInstance(summary.cpuType);
  if (summary.cpuType == "SPR")
  {
    auto cpuId = getCpuId(inputJson);
    if (cpuId)
      cpu->cpuId = cpuId.value();
  }

  if (cpu != nullptr)
  {
    // Gather address_map if exists
    summary.memoryMap = cpu->getMemoryMap(inputJson);
    // Analyze MCA registers
    summary.mca = cpu->analyzeMca(inputJson);
    // Analyze correctable PCIe AER registers
    summary.corAer = cpu->analyzeCorAer(inputJson);
    // Analyze uncorrectable PCIe AER registers
    summary.uncAer = cpu->analyzeUncAer(inputJson);
    // Get TSC registers data
    summary.tsc = cpu->getTscData(inputJson);
    // Get Package Therm Status data
    auto thermStatus = cpu->getThermData(inputJson);
    if (thermStatus)
    {
        summary.thermStatus = *thermStatus;
    }
  }
  return summary;
}

json prepareReport(Summary summary, const json& inputJson)
{
    json deviceMap = NULL;
    json silkscreenMap = NULL;
    char* binaryName;

    #ifdef _WIN32
        binaryName = (char*)"bafi.exe";
    #else
        binaryName = (char*)"./bafi";
    #endif

    PciBdfLookup PciBdfLookupMem(binaryName, true, MEMORY_MAP_FILENAME);

    PciBdfLookup PciBdfLookupDev(binaryName, true, DEVICE_MAP_FILENAME);
    deviceMap = PciBdfLookupDev.lookupMap;

    PciBdfLookup PciBdfLookupSil(binaryName, true, SILKSCREEN_MAP_FILENAME);
    silkscreenMap = PciBdfLookupSil.lookupMap;

    auto cpu = CpuFactory::createInstance(summary.cpuType);
    // Analyze TOR data in CPU sections
    auto tor = cpu->analyze(inputJson);
    // Initialize report from collected data
    Report<TORData> report(summary, tor, summary.cpuType, cpu->cpuId, deviceMap,
                           silkscreenMap, inputJson);
    // Generate JSON report from collected data
    json reportJson = report.createJSONReport();

    return reportJson;
}

void findSocketsFromSummary(std::vector<std::string> &sockets, json reportJson)
{
  for (const auto& metaDataItem : reportJson["summary"].items())
  {
    if (startsWith(metaDataItem.key(), "socket"))
    {
        sockets.push_back(metaDataItem.key());
    }
  }
}

std::string findFirstErrorSocket(json reportJson, std::string &firstError)
{
  std::vector<std::string> sockets;
  bool ierr = false;
  bool mcerr = false;
  bool rmcaMcerr = false;
  std::string firstSocket;

  findSocketsFromSummary(sockets, reportJson);

  for (const auto& socket : sockets)
  {
    for (const auto& [pciKey, pciVal] : reportJson["summary"][socket].items())
    {
      if (pciVal.contains("TSC"))
      {
        for (const auto& [metaKey, metaVal] : pciVal.items())
        {
          for (const auto& [tscKey, tscVal] : metaVal.items())
          {
            if (std::string(tscVal).find("(Occurred first between all TSCs)") !=
            std::string::npos)
            {
              if (std::string(tscVal).find("pcu_first_ierr_tsc_cfg") !=
              std::string::npos)
              {
                firstError = "FirstIERR";
                ierr = true;
              }
              else if (std::string(tscVal).find("pcu_first_mcerr_tsc_cfg") !=
              std::string::npos)
              {
                firstError = "FirstMCERR";
                mcerr = true;
              }
              else if (std::string(tscVal).find("pcu_first_rmca_tsc_cfg") !=
              std::string::npos)
              {
                firstError = "RMCA_FirstMCERR";
                rmcaMcerr = true;
              }
              else
              {
                firstError = "FirstMCERR";
                mcerr = true;
              }
              firstSocket = socket;
            }
          }
        }
      }
    }
  }

  if (ierr || mcerr || rmcaMcerr)
    return firstSocket;
  else
    return "";
}

std::string findBankNumber(std::stringstream &ss)
{
  std::string bankNumber;
  int found;
  std::stringstream ss2;
  std::size_t left = ss.str().find("bank");
  if (left > ss.str().size())
    return "";
  ss2 << ss.str().substr(left, 7);
  while (!ss2.eof())
  {
    ss2 >> bankNumber;
    if (std::stringstream(bankNumber) >> found)
    {
      break;
    }
    bankNumber = "";
  }
  return bankNumber;
}

#ifdef AMI_CODE
void getErrorStrings(std::string entry, std::string &ierr, std::string &mcerr)
{
  std::size_t left = std::string(entry).find("FirstIERR");

  std::size_t right = std::string(entry)
  .substr(left, std::string(entry).size()).find(", FirstMCERR");
  ierr = std::string(entry)
  .substr(left, right).substr(12);

  left = std::string(entry).find("FirstMCERR");
  mcerr = std::string(entry)
  .substr(left + 13, std::string(entry).size());
}

std::string determineFirstErrorType(std::string entry, std::string &ierr,
                                    std::string &mcerr)
{
  getErrorStrings(entry, ierr, mcerr);

  if (ierr != "0x0" && mcerr == "0x0")
    return "FirstIERR";
  else
    return "FirstMCERR";
}
#else
void getErrorStrings(std::string entry, std::string &ierr, std::string &mcerr,
                     std::string &rmcaMcerr)
{
  std::size_t left = std::string(entry).find("FirstIERR");

  std::size_t right = std::string(entry)
  .substr(left, std::string(entry).size()).find(", FirstMCERR");
  ierr = std::string(entry)
  .substr(left, right).substr(12);

  left = std::string(entry).find("FirstMCERR");
  right = std::string(entry)
  .substr(left, std::string(entry).size()).find(", RMCA_FirstMCERR");
  mcerr = std::string(entry)
  .substr(left, right).substr(13);

  left = std::string(entry).find("RMCA_FirstMCERR");
  rmcaMcerr = std::string(entry)
  .substr(left, std::string(entry).size()).substr(18);
}
std::string determineFirstErrorType(std::string entry, std::string &ierr,
                                    std::string &mcerr, std::string &rmcaMcerr)
{
  getErrorStrings(entry, ierr, mcerr, rmcaMcerr);
  if (ierr != "0x0" && mcerr == "0x0")
    return "FirstIERR";
  else if (mcerr != "0x0")
    return "FirstMCERR";
  else
    return "RMCA_FirstMCERR";
}

#endif
void getMemoryErrors(json reportJson, std::string socket,
                     nlohmann::ordered_json &triageReport)
{
  bool corMemErr = false;
  bool uncorMemErr = false;
  for (const auto& [pciKey, pciVal] : reportJson["summary"][socket].items())
  {
    if (pciVal.contains("Memory errors"))
    {
      for (const auto& [metaKey, metaVal] : pciVal.items())
      {
        for (const auto& [key, val] : metaVal.items())
        {
          for (const auto& [strKey, strVal] : val.items())
          {
            if (!startsWith(strVal, "IMC"))
            {
              std::string slotName = std::string(strVal)
                .substr(0, std::string(strVal).find(","));
              if (std::string(key) == "Correctable")
              {
                triageReport[socket]["CorrectableMemoryErrors"]
                .push_back(slotName);
                corMemErr = true;
              }
              else
              {
                triageReport[socket]["UncorrectableMemoryErrors"]
                .push_back(slotName);
                uncorMemErr = true;
              }
            }
          }
        }
      }
    }
  }
  if (!corMemErr)
  {
    triageReport[socket]["CorrectableMemoryErrors"] = json::object();
  }
  if (!uncorMemErr)
  {
    triageReport[socket]["UncorrectableMemoryErrors"] = json::object();
  }
}
#ifdef AMI_CODE
void getThermalErrors(json reportJson, std::string socket,
    nlohmann::ordered_json &triageReport, Summary& summary)
{
  uint32_t socketId = std::stoi(socket.substr(6));
  if (summary.thermStatus.find(socketId) == summary.thermStatus.end())
  {
    triageReport[socket]["PackageThermalStatus"] = json::object();
  }
  else
  {
    if (!bool(summary.thermStatus[socketId].prochot_log)
      && !bool(summary.thermStatus[socketId].prochot_status)
      && !bool(summary.thermStatus[socketId].pmax_log)
      && !bool(summary.thermStatus[socketId].pmax_status)
      && !bool(summary.thermStatus[socketId].out_of_spec_log)
      && !bool(summary.thermStatus[socketId].out_of_spec_status)
      && !bool(summary.thermStatus[socketId].thermal_monitor_log)
      && !bool(summary.thermStatus[socketId].thermal_monitor_status))
    {
      triageReport[socket]["PackageThermalStatus"] = "Ok";
    }
    else
    {
      triageReport[socket]["PackageThermalStatus"] = "Error";
    }
  }
}
#else
void getThermalErrors(std::string socket,
    nlohmann::ordered_json &triageReport, Summary& summary)
{
  uint32_t socketId = std::stoi(socket.substr(6));
  if (summary.thermStatus.find(socketId) == summary.thermStatus.end())
  {
    triageReport[socket]["PackageThermalStatus"] = json::object();
  }
  else
  {
    if (!bool(summary.thermStatus[socketId].prochot_log)
      && !bool(summary.thermStatus[socketId].prochot_status)
      && !bool(summary.thermStatus[socketId].pmax_log)
      && !bool(summary.thermStatus[socketId].pmax_status)
      && !bool(summary.thermStatus[socketId].out_of_spec_log)
      && !bool(summary.thermStatus[socketId].out_of_spec_status)
      && !bool(summary.thermStatus[socketId].thermal_monitor_log)
      && !bool(summary.thermStatus[socketId].thermal_monitor_status))
    {
      triageReport[socket]["PackageThermalStatus"] = "Ok";
    }
    else
    {
      triageReport[socket]["PackageThermalStatus"] = "Error";
    }
  }
}
#endif

uint32_t getFullOutput(char* inputPointer, std::size_t inputSize,
                        char*& fullOutput, size_t* fullOutputSize,
                        json deviceMap)
{
  if (inputPointer == nullptr || inputSize == 0)
    return CRASHDUMP_NOT_PROVIDED;

  std::string inputStr(inputPointer);
  inputStr.erase(std::remove_if(inputStr.begin(),
    inputStr.end(), ::isspace), inputStr.end());

  auto inputJson = json::parse(inputStr, nullptr, false);
  if (inputJson.is_discarded())
    return FAILED_TO_PARSE_CRASHDUMP_TO_JSON;

  auto input = getProperRootNode(inputJson);
  if (!input)
    return INCORRECT_JSON_ROOT_NODE;

  auto summary = prepareSummary(*input);
  json reportJson = prepareReport(summary, *input);
  if (reportJson.is_discarded())
    return FAILED_TO_PREPARE_REPORT;

  // Assign full output to char pointer
  std::string outputStr = reportJson.dump(4);
  std::size_t fullOutputSizeTemp = strlen(outputStr.c_str());
  *fullOutputSize = fullOutputSizeTemp;
  fullOutput = new char[*fullOutputSize + 1];
  strncpy(fullOutput, outputStr.c_str(), *fullOutputSize + 1);

  return SUCCESS;
}
#ifdef AMI_CODE
nlohmann::ordered_json getTriageInformation(json reportJson, const json& input,
    json deviceMap, Summary& summary)
{
  nlohmann::ordered_json triageReport;
  std::string firstError = "";
  std::stringstream ss;
  std::stringstream ss2;
  std::regex core_regex("([0-9])-([0-9])");
  std::smatch matches;
  bool imc_present = false;
  bool aerCorSection = false;
  bool aerUncorSection = false;
  bool fru_section = false;
  std::string imc_string;
  std::string bankNumber;
  std::string ierr;
  std::string mcerr;
  std::string rmcaMcerr;
  std::vector<std::string> sockets;
  std::string firstErrorSocket = findFirstErrorSocket(reportJson, firstError);

  if (firstErrorSocket != "")
  {
    triageReport["First_Error_Occurred_On_Socket"] = firstErrorSocket.substr(6);
    findSocketsFromSummary(sockets, reportJson);
  }
  else
  {
    firstError = "FirstMCERR";
    findSocketsFromSummary(sockets, reportJson);
    if (sockets.size() > 1)
      triageReport["First_Error_Occurred_On_Socket"] = "N/A";
    else
      triageReport["First_Error_Occurred_On_Socket"] = "0";
  }

  // Get descriptive information for each error
  for (const auto& socket : sockets)
  {
    ss.str("");
    ss << "S" << socket.substr(6) << ".";
    for (const auto& [pciKey, pciVal] : reportJson["summary"][socket].items())
    {
      if (pciVal.contains("Errors_Per_Socket"))
      {
        for (const auto& [metaKey, metaVal] : pciVal.items())
        {
          for (const auto& [tscKey, tscVal] : metaVal.items())
          {
            if (firstErrorSocket == "")
            {
              firstError =
                //determineFirstErrorType(std::string(tscVal), ierr, mcerr, rmcaMcerr);
		            determineFirstErrorType(std::string(tscVal), ierr, mcerr);
              //if (sockets.size() == 1 && ierr == "0x0" && mcerr == "0x0" && rmcaMcerr == "0x0")
		          if (sockets.size() == 1 && ierr == "0x0" && mcerr == "0x0")
                triageReport["First_Error_Occurred_On_Socket"] = "N/A";
            }
            else
            {
              getErrorStrings(std::string(tscVal), ierr, mcerr);
            }
            std::size_t left = std::string(tscVal).find(firstError);
            if (left > std::string(tscVal).size())
              continue;
            if (firstError == "FirstIERR")
            {
              std::size_t right = std::string(tscVal)
              .substr(left, std::string(tscVal).size()).find(", FirstMCERR");
              if (ierr == "0x0")
              {
                ss << "N/A";
                break;
              }
              ss << "IERR.";
              if (std::string(tscVal).substr(left, right).find("IMC")
                != std::string::npos)
              {
                ss2 << std::string(tscVal).substr(left, right);
                imc_present = true;
                bankNumber = findBankNumber(ss2);
                break;
              }
              ss << ierr;
              bankNumber = findBankNumber(ss);
              break;
            }
            else
            {
              if (mcerr == "0x0" || (firstError == "RMCA_FirstMCERR" && rmcaMcerr != "0x0"))
              {
                mcerr = rmcaMcerr;
              }
              if (mcerr == "0x0")
              {
                ss << "N/A";
                break;
              }

              if (std::string(mcerr).find("manual review may be required") !=
                  std::string::npos)
              {
                ss << "FirstMCERR != MCE bank#, manual review required";
                break;
              }
              ss << "MCERR.";
              if (std::string(tscVal).substr(left,
                std::string(tscVal).size()).find("IMC") != std::string::npos)
              {
                ss2 << std::string(tscVal).substr(left,
                                                  std::string(tscVal).size());
                imc_present = true;
                bankNumber = findBankNumber(ss2);
                break;
              }
              ss << mcerr;
              bankNumber = findBankNumber(ss);
              if (bankNumber == "" && (mcerr.find("CHA") != std::string::npos ||
                mcerr.find("M2MEM") != std::string::npos))
              {
                bankNumber = mcerr;
              }
              break;
            }
          }
        }
      }
    }

    for (const auto& [pciKey, pciVal] : reportJson["summary"][socket].items())
    {
      if (pciVal.contains("MCE"))
      {
        for (const auto& [metaKey, metaVal] : pciVal.items())
        {
          for (const auto& [tscKey, tscVal] : metaVal.items())
          {
            if (bankNumber == "9" || bankNumber == "10" || bankNumber == "11")
            {
              if (std::string(tscVal).find("bank " + bankNumber) !=
                  std::string::npos)
              {
                std::size_t left = std::string(tscVal).find("RAW ");
                if (left > std::string(tscVal).size())
                  continue;
                std::size_t right =
                  std::string(tscVal).find(std::string(tscVal).size());
                bankNumber = std::string(tscVal).substr(left + 4, left-right);
              }
            }
            std::string core_string = ss.str();
            if (std::regex_search(core_string, matches, core_regex))
            {
              for (int i = std::stoi(matches[1].str());
                i <= std::stoi(matches[2].str()); i++)
              {
                if (std::string(tscVal).find("bank " + std::to_string(i)) !=
                  std::string::npos)
                {
                  bankNumber = std::to_string(i);
                  break;
                }
              }
            }
            if (imc_present)
            {
              if (std::string(tscVal).find("bank " + bankNumber)
                != std::string::npos)
              {
                std::size_t left = std::string(tscVal).find("bank");
                if (left > std::string(tscVal).size())
                  continue;
                std::size_t right = std::string(tscVal).find(")");
                imc_string = std::string(tscVal).substr(left, right + 1);
                ss << imc_string;
                bankNumber = findBankNumber(ss);
                break;
              }
              else
              {
                std::stringstream ss3;
                ss3 << ss2.str().erase(ss2.str().find("bank"), 3);
                ss2.str("");
                ss2 << ss3.str();
                bankNumber = findBankNumber(ss2);
              }
            }
          }
        }
      }
    }

    // Get MSCOD and MCACOD
    std::string mscod;
    std::string mcacod;
    std::string msec_fw = "";
    if (bankNumber.find("CHA") == std::string::npos)
      bankNumber = bankNumber + " ";

    for (const auto& [pciKey, pciVal] : reportJson["MCA"][socket].items())
    {
      for (const auto& [mcaKey, mcaVal] : pciVal.items())
      {
        if (mcaKey == "Bank")
        {
          if (std::string(mcaVal).rfind(bankNumber, 0) != 0 || bankNumber == "")
          {
            break;
          }
          if (bankNumber == "4")
          {
            mscod = "";
            mcacod = pciVal["Status_decoded"]["MCACOD"];
            msec_fw = pciVal["Status_decoded"]["MSEC_FW"];
            break;
          }
        }
        if (mcaKey == "Status")
        {
          mscod = std::string(pciVal["Status"]).substr(10, 4);
          mcacod = std::string(pciVal["Status"]).substr(14, 4);
          if (mcacod == "")
            break;
          while (mscod.front() == '0')
          {
            mscod.erase(0, 1);
            if (mscod == "")
              break;
          }
          while (mcacod.front() == '0')
          {
            mcacod.erase(0, 1);
            if (mcacod == "")
              break;
          }
          if (mscod == "")
          {
            mscod = "00";
          }
          if (mcacod == "")
          {
            mcacod = "00";
          }
          mscod = "0x" + mscod;
          mcacod = "0x" + mcacod;
          break;
        }
      }
    }

    if (mcacod != "")
    {
      if (msec_fw != "")
        ss << " MCACOD: " << mcacod << " MSEC_FW: " << msec_fw;
      else
        ss << " MCACOD: " << mcacod << " MSCOD: " << mscod;
    }

    nlohmann::ordered_json error_info = ss.str();
    triageReport[socket]["Error_Information"].push_back(error_info);

    // Get FRU section in case IMC bank is present in errors
    std::stringstream ss_fru;
    std::stringstream ss_aer;

    if (imc_present)
    {
      std::size_t left = std::string(imc_string).find("bank");
      if (left <= std::string(imc_string).size())
      {
        std::size_t right = std::string(imc_string).find(")");
        ss_fru << std::string(imc_string).substr(left + 8, right + 1);
        nlohmann::ordered_json fru_info = ss_fru.str();
        triageReport[socket]["FRU"].push_back(fru_info);
        fru_section = true;
      }
    }

    // Get FRU section for all other banks
    for (const auto& [pciKey, pciVal] : reportJson["summary"][socket].items())
    {
      if (pciVal.contains("First_MCERR"))
      {
        auto bdfObj = nlohmann::ordered_json::object();
        bdfObj["Address"] = pciVal["First_MCERR"]["Address"];
        std::string bdf_string = pciVal["First_MCERR"]["BDF"];
        if (bdf_string == "Please refer to system address map")
        {
          bdfObj["Bus"] = bdf_string;
          bdfObj["Device"] = bdf_string;
          bdfObj["Function"] = bdf_string;
          triageReport[socket]["FRU"].push_back(bdfObj);
        }
        else
        {
          getBdfFromFirstMcerrSection(bdf_string, bdfObj);
          showBdfDescription(deviceMap, bdfObj);
          triageReport[socket]["FRU"].push_back(bdfObj);
        }
        fru_section = true;
      }
      if (pciVal.contains("IO_Errors"))
      {
        std::string bdf_string = pciVal["IO_Errors"][0];
        auto bdfObj = nlohmann::ordered_json::object();
        getBdfFromIoErrorsSection(bdf_string, bdfObj);
        showBdfDescription(deviceMap, bdfObj);
        triageReport[socket]["FRU"].push_back(bdfObj);
        fru_section = true;
      }
    }
    if (!fru_section)
    {
      triageReport[socket]["FRU"] = json::object();
    }
    fru_section = false;
  }
  // Get AER sections
  sockets.clear();
  findSocketsFromSummary(sockets, reportJson);
  for (const auto& socket : sockets)
  {
    getMemoryErrors(reportJson, socket, triageReport);
    for (const auto& [pciKey, pciVal] : reportJson["summary"][socket].items())
    {
      if (pciVal.contains("PCIe_AER_Uncorrectable_errors"))
      {
        triageReport[socket]["PcieAerUncorrectable"]
        = pciVal["PCIe_AER_Uncorrectable_errors"];
        aerUncorSection = true;
      }
      if (pciVal.contains("PCIe_AER_Correctable_errors"))
      {
        triageReport[socket]["PcieAerCorrectable"]
        = pciVal["PCIe_AER_Correctable_errors"];
        aerCorSection = true;
      }
    }

    if (!aerCorSection)
    {
      triageReport[socket]["PcieAerCorrectable"] = json::object();
    }

    if (!aerUncorSection)
    {
      triageReport[socket]["PcieAerUncorrectable"] = json::object();
    }

    getThermalErrors(reportJson, socket, triageReport, summary);
    aerUncorSection = false;
    aerCorSection = false;
}
  return triageReport;
}
#else

#endif